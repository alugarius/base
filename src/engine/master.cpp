#ifdef WIN32
#define FD_SETSIZE 4096
#else
#include <sys/types.h>
#undef __FD_SETSIZE
#define __FD_SETSIZE 4096
#endif

#include "engine.h"
#include <enet/time.h>
#include <sqlite3.h>

#define STATSDB_VERSION 1
#define MASTER_LIMIT 4096
#define CLIENT_TIME (60*1000)
#define SERVER_TIME (35*60*1000)
#define AUTH_TIME (30*1000)

VAR(0, masterserver, 0, 0, 1);
VAR(0, masterport, 1, MASTER_PORT, VAR_MAX);
VAR(0, masterminver, 0, 0, CUR_VERSION);
SVAR(0, masterip, "");
SVAR(0, masterscriptclient, "");
SVAR(0, masterscriptserver, "");

VAR(0, masterduplimit, 0, 3, VAR_MAX);
VAR(0, masterpingdelay, 1000, 3000, VAR_MAX);
VAR(0, masterpingtries, 1, 5, VAR_MAX);

struct authuser
{
    char *name, *flags, *email;
    void *pubkey;
};

struct authreq
{
    enet_uint32 reqtime;
    uint id;
    void *answer;
    authuser *user;
    string hostname;
};

struct masterclient
{
    ENetAddress address;
    ENetSocket socket;
    string name;

    /* Server Flags:
     * b - basic
     * s - statistics
     */
    string flags;
    string authhandle;

    char input[4096];
    vector<char> output;
    int inputpos, outputpos, port, numpings, lastcontrol, version;
    enet_uint32 lastping, lastpong, lastactivity;
    vector<authreq> authreqs;
    authreq serverauthreq;
    bool isserver, isquick, ishttp, listserver, shouldping, shouldpurge;

    struct statstate
    {
        //Game
        sqlite3_int64 id;
        string map;
        int mode, mutators, timeplayed;
        time_t time;
        //Server
        string desc;
        string version;
        int port;
        //Teams
        struct team
        {
            int index, score;
            string name;
        };
        vector<team> teams;
        //Players
        struct player
        {
            string name;
            string handle;
            int score, timealive, frags, deaths;
            int wid;
        };
        vector<player> players;
        //Weapons
        struct weaponstats
        {
            string name;
            int pid;
            int timewielded, timeloadout;
            int hits1, hits2, flakhits1, flakhits2;
            int shots1, shots2, flakshots1, flakshots2;
            int frags1, frags2, damage1, damage2;
        };
        vector<weaponstats> weapstats;
    } stats;

    bool instats;

    bool hasflag(char f)
    {
        //Any flag implies 'b'
        if(f == 'b' && *flags)
            return true;
        size_t i;
        for(i = 0; i < strlen(flags); i++)
        {
            if(flags[i] == f)
                return true;
        }
        return false;
    }

    masterclient() : inputpos(0), outputpos(0), port(MASTER_PORT), numpings(0), lastcontrol(-1), version(0), lastping(0), lastpong(0), lastactivity(0), isserver(false), isquick(false), ishttp(false), listserver(false), shouldping(false), shouldpurge(false), instats(false) {}
};

static vector<masterclient *> masterclients;
static ENetSocket mastersocket = ENET_SOCKET_NULL, pingsocket = ENET_SOCKET_NULL;
static time_t starttime;
static sqlite3 *statsdb = NULL;

void closestatsdb()
{
    if(statsdb)
    {
        sqlite3_close(statsdb);
        statsdb = NULL;
    }
}

bool checkstatsdb(int rc, char *err_msg=NULL)
{
    if(rc == SQLITE_OK)
        return true;
    defformatbigstring(message, "%s", err_msg ? err_msg : sqlite3_errmsg(statsdb));
    sqlite3_free(err_msg);
    closestatsdb();
    fatal("statistics database error: %s", message);
    return false;
}

void statsdbexecf(const char *fmt, ...)
{
    char *err_msg = NULL;
    va_list al;
    va_start(al, fmt);
    char *sql = sqlite3_vmprintf(fmt, al);
    int rc = sqlite3_exec(statsdb, sql, 0, 0, &err_msg);
    sqlite3_free(sql);
    va_end(al);
    checkstatsdb(rc, err_msg);
}

void statsdbexecfile(const char *path)
{
    char *err_msg = NULL;
    char *buf = loadfile(path, NULL);
    if(!buf)
    {
        fatal("cannot find %s", path);
        closestatsdb();
    }
    int rc = sqlite3_exec(statsdb, buf, 0, 0, &err_msg);
    checkstatsdb(rc, err_msg);
    DELETEA(buf);
}

int statsdbversion()
{
    int version = 0;
    sqlite3_stmt *res;
    checkstatsdb(sqlite3_prepare_v2(statsdb, "PRAGMA user_version;", -1, &res, 0));
    while(sqlite3_step(res) == SQLITE_ROW)
    {
        version = sqlite3_column_int(res, 0);
    }
    sqlite3_finalize(res);
    return version;
}

void loadstatsdb()
{
    checkstatsdb(sqlite3_open(findfile("stats.sqlite", "w"), &statsdb));
    statsdbexecf("BEGIN");
    if(statsdbversion() < 1)
    {
        statsdbexecfile("sql/stats/create.sql");
        statsdbexecf("PRAGMA user_version = %d;", STATSDB_VERSION);
        conoutf("created statistics database");
    }
    while(statsdbversion() < STATSDB_VERSION)
    {
        int ver = statsdbversion();
        defformatstring(path, "sql/stats/upgrade_%d.sql", ver);
        statsdbexecfile(path);
        statsdbexecf("PRAGMA user_version = %d;", ver + 1);
        conoutf("upgraded database from %d to %d", ver, statsdbversion());
    }
    statsdbexecf("COMMIT");
    conoutf("statistics database loaded");
}

bool setuppingsocket(ENetAddress *address)
{
    if(pingsocket != ENET_SOCKET_NULL) return true;
    pingsocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(pingsocket == ENET_SOCKET_NULL) return false;
    if(address && enet_socket_bind(pingsocket, address) < 0) return false;
    enet_socket_set_option(pingsocket, ENET_SOCKOPT_NONBLOCK, 1);
    return true;
}

void setupmaster()
{
    if(masterserver)
    {
        conoutf("loading master (%s:%d)..", *masterip ? masterip : "*", masterport);
        ENetAddress address = { ENET_HOST_ANY, enet_uint16(masterport) };
        if(*masterip && enet_address_set_host(&address, masterip) < 0) fatal("failed to resolve master address: %s", masterip);
        if((mastersocket = enet_socket_create(ENET_SOCKET_TYPE_STREAM)) == ENET_SOCKET_NULL) fatal("failed to create master server socket");
        if(enet_socket_set_option(mastersocket, ENET_SOCKOPT_REUSEADDR, 1) < 0) fatal("failed to set master server socket option");
        if(enet_socket_bind(mastersocket, &address) < 0) fatal("failed to bind master server socket");
        if(enet_socket_listen(mastersocket, -1) < 0) fatal("failed to listen on master server socket");
        if(enet_socket_set_option(mastersocket, ENET_SOCKOPT_NONBLOCK, 1) < 0) fatal("failed to make master server socket non-blocking");
        if(!setuppingsocket(&address)) fatal("failed to create ping socket");
        starttime = clocktime;
        loadstatsdb();
        conoutf("master server started on %s:[%d]", *masterip ? masterip : "localhost", masterport);
    }
}

void masterout(masterclient &c, const char *msg, int len = 0)
{
    if(!len) len = strlen(msg);
    c.output.put(msg, len);
}

void masteroutf(masterclient &c, const char *fmt, ...)
{
    bigstring msg;
    va_list args;
    va_start(args, fmt);
    vformatstring(msg, fmt, args);
    va_end(args);
    masterout(c, msg);
}

static hashnameset<authuser> authusers;

void addauth(char *name, char *flags, char *pubkey, char *email)
{
    string authname;
    if(filterstring(authname, name, true, true, true, true, 100)) name = authname;
    if(authusers.access(name))
    {
        conoutf("auth handle '%s' already exists, skipping (%s)", name, email);
        return;
    }
    name = newstring(name);
    authuser &u = authusers[name];
    u.name = name;
    u.flags = newstring(flags);
    u.pubkey = parsepubkey(pubkey);
    u.email = newstring(email);
}
COMMAND(0, addauth, "ssss");

static hashnameset<authuser> serverauthusers;

void addserverauth(char *name, char *flags, char *pubkey, char *email)
{
    string authname;
    if(filterstring(authname, name, true, true, true, true, 100)) name = authname;
    if(serverauthusers.access(name))
    {
        conoutf("server auth handle '%s' already exists, skipping (%s)", name, email);
        return;
    }
    name = newstring(name);
    authuser &u = serverauthusers[name];
    u.name = name;
    u.flags = newstring(flags);
    u.pubkey = parsepubkey(pubkey);
    u.email = newstring(email);
}
COMMAND(0, addserverauth, "ssss");

void clearauth()
{
    enumerate(authusers, authuser, u, { delete[] u.name; delete[] u.flags; delete[] u.email; freepubkey(u.pubkey); });
    authusers.clear();
    enumerate(serverauthusers, authuser, u, { delete[] u.name; delete[] u.flags; delete[] u.email; freepubkey(u.pubkey); });
    serverauthusers.clear();
}
COMMAND(0, clearauth, "");

void purgeauths(masterclient &c)
{
    int expired = 0;
    loopv(c.authreqs)
    {
        if(ENET_TIME_DIFFERENCE(totalmillis, c.authreqs[i].reqtime) >= AUTH_TIME)
        {
            masteroutf(c, "failauth %u\n", c.authreqs[i].id);
            freechallenge(c.authreqs[i].answer);
            expired = i + 1;
        }
        else break;
    }
    if(expired > 0) c.authreqs.remove(0, expired);
}

void reqauth(masterclient &c, uint id, char *name, char *hostname)
{
    string ip, host;
    if(enet_address_get_host_ip(&c.address, ip, sizeof(ip)) < 0) copystring(ip, "-");
    copystring(host, hostname && *hostname ? hostname : "-");

    authuser *u = authusers.access(name);
    if(!u)
    {
        masteroutf(c, "failauth %u\n", id);
        conoutf("failed '%s' (%u) from %s on server %s (NOTFOUND)\n", name, id, host, ip);
        return;
    }
    conoutf("attempting '%s' (%u) from %s on server %s\n", name, id, host, ip);

    authreq &a = c.authreqs.add();
    a.user = u;
    a.reqtime = totalmillis;
    a.id = id;
    copystring(a.hostname, host);
    uint seed[3] = { uint(starttime), uint(totalmillis), randomMT() };
    static vector<char> buf;
    buf.setsize(0);
    a.answer = genchallenge(u->pubkey, seed, sizeof(seed), buf);

    masteroutf(c, "chalauth %u %s\n", id, buf.getbuf());
}

void reqserverauth(masterclient &c, char *name)
{
    purgeauths(c);

    string ip;
    if(enet_address_get_host_ip(&c.address, ip, sizeof(ip)) < 0) copystring(ip, "-");

    authuser *u = serverauthusers.access(name);
    if(!u)
    {
        masteroutf(c, "failserverauth\n");
        conoutf("failed server '%s' (NOTFOUND)\n", name);
        return;
    }
    conoutf("attempting server '%s'\n", name);

    c.serverauthreq.user = u;
    c.serverauthreq.reqtime = totalmillis;
    uint seed[3] = { uint(starttime), uint(totalmillis), randomMT() };
    static vector<char> buf;
    buf.setsize(0);
    c.serverauthreq.answer = genchallenge(u->pubkey, seed, sizeof(seed), buf);

    masteroutf(c, "chalserverauth %s\n", buf.getbuf());
}

void confauth(masterclient &c, uint id, const char *val)
{
    purgeauths(c);
    string ip;
    if(enet_address_get_host_ip(&c.address, ip, sizeof(ip)) < 0) copystring(ip, "-");
    loopv(c.authreqs) if(c.authreqs[i].id == id)
    {
        if(checkchallenge(val, c.authreqs[i].answer))
        {
            masteroutf(c, "succauth %u \"%s\" \"%s\"\n", id, c.authreqs[i].user->name, c.authreqs[i].user->flags);
            conoutf("succeeded '%s' [%s] (%u) from %s on server %s\n", c.authreqs[i].user->name, c.authreqs[i].user->flags, id, c.authreqs[i].hostname, ip);
        }
        else
        {
            masteroutf(c, "failauth %u\n", id);
            conoutf("failed '%s' (%u) from %s on server %s (BADKEY)\n", c.authreqs[i].user->name, id, c.authreqs[i].hostname, ip);
        }
        freechallenge(c.authreqs[i].answer);
        c.authreqs.remove(i--);
        return;
    }
    masteroutf(c, "failauth %u\n", id);
}

void purgemasterclient(int n)
{
    masterclient &c = *masterclients[n];
    enet_socket_destroy(c.socket);
    if(verbose || c.isserver) conoutf("master peer %s disconnected", c.name);
    delete masterclients[n];
    masterclients.remove(n);
}

void confserverauth(masterclient &c, const char *val)
{
    string ip;
    if(enet_address_get_host_ip(&c.address, ip, sizeof(ip)) < 0) copystring(ip, "-");
    if(checkchallenge(val, c.serverauthreq.answer))
    {
        loopvj(masterclients) if(!(!strcmp(c.name, masterclients[j]->name) && c.port == masterclients[j]->port))
        {
            if(!strcmp(masterclients[j]->authhandle, c.serverauthreq.user->name))
                purgemasterclient(j);
        }
        masteroutf(c, "succserverauth \"%s\" \"%s\"\n", c.serverauthreq.user->name, c.serverauthreq.user->flags);
        conoutf("succeeded server '%s' [%s]\n", c.serverauthreq.user->name, c.serverauthreq.user->flags);
        copystring(c.authhandle, c.serverauthreq.user->name);
        copystring(c.flags, c.serverauthreq.user->flags);
    }
    else
    {
        masteroutf(c, "failserverauth\n");
        conoutf("failed server '%s' (BADKEY)\n", c.serverauthreq.user->name);
    }
    freechallenge(c.serverauthreq.answer);
}

void checkmasterpongs()
{
    ENetBuffer buf;
    ENetAddress addr;
    static uchar pong[MAXTRANS];
    for(;;)
    {
        buf.data = pong;
        buf.dataLength = sizeof(pong);
        int len = enet_socket_receive(pingsocket, &addr, &buf, 1);
        if(len <= 0) break;
        loopv(masterclients)
        {
            masterclient &c = *masterclients[i];
            if(c.address.host == addr.host && c.port+1 == addr.port)
            {
                c.lastpong = totalmillis ? totalmillis : 1;
                c.listserver = true;
                c.shouldping = false;
                masteroutf(c, "echo \"ping reply confirmed (on port %d), server is now listed\"\n", addr.port);
                conoutf("master peer %s responded to ping request on port %d successfully",  c.name, addr.port);
                break;
            }
        }
    }
}

static int controlversion = 0;

int nextcontrolversion()
{
    ++controlversion;
    if(controlversion < 0)
    {
        controlversion = 0;
        loopv(masterclients) masterclients[i]->lastcontrol = -1;
        loopv(control) if(control[i].type < ipinfo::SYNCTYPES && control[i].flag == ipinfo::LOCAL) control[i].version = controlversion;
    }
    return controlversion;
}

bool checkmasterclientinput(masterclient &c)
{
    if(c.inputpos < 0) return false;
    const int MAXWORDS = 24;
    char *w[MAXWORDS];
    int numargs = MAXWORDS;
    const char *p = c.input;
    for(char *end;; p = end)
    {
        end = (char *)memchr(p, '\n', &c.input[c.inputpos] - p);
        if(!end) end = (char *)memchr(p, '\0', &c.input[c.inputpos] - p);
        if(!end) break;
        *end++ = '\0';
        if(c.ishttp) continue; // eat up the rest of the bytes, we've done our bit

        //conoutf("{%s} %s", c.name, p);
        loopi(MAXWORDS)
        {
            w[i] = (char *)"";
            if(i > numargs) continue;
            char *s = parsetext(p);
            if(s) w[i] = s;
            else numargs = i;
        }

        p += strcspn(p, ";\n\0"); p++;

        if(!strcmp(w[0], "GET") && w[1] && *w[1] == '/') // cheap server-to-http hack
        {
            loopi(numargs)
            {
                if(i)
                {
                    if(i == 1)
                    {
                        char *q = newstring(&w[i][1]);
                        delete[] w[i];
                        w[i] = q;
                    }
                    w[i-1] = w[i];
                }
                else delete[] w[i];
            }
            w[numargs-1] = (char *)"";
            numargs--;
            c.ishttp = c.shouldpurge = true;
        }
        bool found = false, server = !strcmp(w[0], "server");
        if((server || !strcmp(w[0], "quick")) && !c.ishttp)
        {
            c.port = SERVER_PORT;
            c.lastactivity = totalmillis ? totalmillis : 1;
            if(!server)
            {
                c.isquick = true;
                masteroutf(c, "echo \"session initiated, awaiting auth requests\"\n");
                conoutf("master peer %s quickly checking auth request",  c.name);
            }
            else
            {
                if(w[1] && *w[1]) c.port = clamp(atoi(w[1]), 1, VAR_MAX);
                c.version = w[3] && *w[3] ? atoi(w[3]) : (w[2] && *w[2] ? 150 : 0);
                ENetAddress address = { ENET_HOST_ANY, enet_uint16(c.port) };
                if(w[2] && *w[2] && strcmp(w[2], "*") && (enet_address_set_host(&address, w[2]) < 0 || address.host != c.address.host))
                {
                    c.listserver = c.shouldping = false;
                    masteroutf(c, "echo \"server IP '%s' does not match origin '%s', server will not be listed\n", w[2], c.name);
                }
                else if(masterminver && c.version < masterminver)
                {
                    c.listserver = c.shouldping = false;
                    masteroutf(c, "echo \"server version '%d' is no longer supported (need: %d), please update at %s\n", c.version, masterminver, versionurl);
                }
                else
                {
                    c.shouldping = true;
                    c.numpings = 0;
                    c.lastcontrol = controlversion;
                    loopv(control) if(control[i].type < ipinfo::SYNCTYPES && control[i].flag == ipinfo::LOCAL)
                        masteroutf(c, "%s %u %u \"%s\"\n", ipinfotypes[control[i].type], control[i].ip, control[i].mask, control[i].reason);
                    if(c.isserver)
                    {
                        masteroutf(c, "echo \"server updated (port %d), sending ping request (on port %d)\"\n", c.port, c.port+1);
                        conoutf("master peer %s updated server info (%d)",  c.name, c.port);
                    }
                    else
                    {
                        if(*masterscriptserver) masteroutf(c, "%s\n", masterscriptserver);
                        masteroutf(c, "echo \"server registered (port %d), sending ping request (on port %d)\"\n", c.port, c.port+1);
                        conoutf("master peer %s registered as a server (%d)", c.name, c.port);
                    }
                    c.isserver = true;
                }
            }
            found = true;
        }
        if(!strcmp(w[0], "version") || !strcmp(w[0], "update"))
        {
            masteroutf(c, "setversion %d %d\n", server::getver(0), server::getver(1));
            if(*masterscriptclient && !strcmp(w[0], "update")) masteroutf(c, "%s\n", masterscriptclient);
            if(verbose) conoutf("master peer %s was sent the version",  c.name);
            c.shouldpurge = found = true;
        }
        if(!strcmp(w[0], "list") || !strcmp(w[0], "update"))
        {
            int servs = 0;
            masteroutf(c, "clearservers\n");
            loopvj(masterclients)
            {
                masterclient &s = *masterclients[j];
                if(!s.listserver) continue;
                masteroutf(c, "addserver %s %d\n", s.name, s.port);
                if(*s.authhandle)
                {
                    masteroutf(c, "authserver %s %d %s\n", s.name, s.port, s.authhandle);
                }
                servs++;
            }
            conoutf("master peer %s was sent %d server(s)", c.name, servs);
            c.shouldpurge = found = true;
        }
        if(c.isserver && !strcmp(w[0], "stats"))
        {
            if(!strcmp(w[1], "begin"))
            {
                if(c.hasflag('s'))
                {
                    conoutf("master peer %s began sending stats", c.name);
                    c.instats = true;
                }
                else
                {
                    conoutf("master peer %s attempted to send stats without proper privilege", c.name);
                    simpleencode(msg_enc, "\frstatistics not submitted, no statistics privilege");
                    masteroutf(c, "stats failure %s\n", msg_enc);
                }
            }
            else if(c.instats)
            {
                if(!strcmp(w[1], "end"))
                {
                    statsdbexecf("BEGIN");
                    statsdbexecf("INSERT INTO games VALUES (NULL, %d, %Q, %d, %d, %d)",
                        c.stats.time,
                        c.stats.map,
                        c.stats.mode,
                        c.stats.mutators,
                        c.stats.timeplayed
                        );
                    c.stats.id = sqlite3_last_insert_rowid(statsdb);

                    statsdbexecf("INSERT INTO game_servers VALUES (%d, %Q, %Q, %Q, %Q, %Q, %d)",
                        c.stats.id,
                        c.authhandle,
                        c.flags,
                        c.stats.desc,
                        c.stats.version,
                        c.name,
                        c.stats.port
                        );

                    loopv(c.stats.teams)
                    {
                        statsdbexecf("INSERT INTO game_teams VALUES (%d, %d, %d, %Q)",
                            c.stats.id,
                            c.stats.teams[i].index,
                            c.stats.teams[i].score,
                            c.stats.teams[i].name
                            );
                    }

                    loopv(c.stats.players)
                    {
                        statsdbexecf("INSERT INTO game_players VALUES (%d, %Q, %Q, %d, %d, %d, %d, %d)",
                            c.stats.id,
                            c.stats.players[i].name,
                            c.stats.players[i].handle,
                            c.stats.players[i].score,
                            c.stats.players[i].timealive,
                            c.stats.players[i].frags,
                            c.stats.players[i].deaths,
                            c.stats.players[i].wid
                        );
                    }

                    loopv(c.stats.weapstats)
                    {
                        statsdbexecf("INSERT INTO game_weapons VALUES (%d, %d, %Q, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)",
                            c.stats.id,
                            c.stats.weapstats[i].pid,
                            c.stats.weapstats[i].name,

                            c.stats.weapstats[i].timewielded,
                            c.stats.weapstats[i].timeloadout,

                            c.stats.weapstats[i].damage1,
                            c.stats.weapstats[i].frags1,
                            c.stats.weapstats[i].hits1,
                            c.stats.weapstats[i].flakhits1,
                            c.stats.weapstats[i].shots1,
                            c.stats.weapstats[i].flakshots1,

                            c.stats.weapstats[i].damage2,
                            c.stats.weapstats[i].frags2,
                            c.stats.weapstats[i].hits2,
                            c.stats.weapstats[i].flakhits2,
                            c.stats.weapstats[i].shots2,
                            c.stats.weapstats[i].flakshots2
                        );
                    }

                    statsdbexecf("COMMIT");
                    conoutf("master peer %s commited stats, game id %lli", c.name, c.stats.id);
                    defformatstring(msg, "\fygame statistics recorded, id \fc%lli", c.stats.id);
                    simpleencode(msg_enc, msg);
                    masteroutf(c, "stats success %s\n", msg_enc);
                    c.instats = false;
                }
                else if(!strcmp(w[1], "game"))
                {
                    simpledecode(mapname_dec, w[2]);
                    copystring(c.stats.map, mapname_dec);
                    c.stats.mode = (int)strtol(w[3], NULL, 10);
                    c.stats.mutators = (int)strtol(w[4], NULL, 10);
                    c.stats.timeplayed = (int)strtol(w[5], NULL, 10);
                    c.stats.time = currenttime;
                }
                else if(!strcmp(w[1], "server"))
                {
                    simpledecode(desc_dec, w[2]);
                    copystring(c.stats.desc, desc_dec);
                    copystring(c.stats.version, w[3]);
                    c.stats.port = (int)strtol(w[4], NULL, 10);
                }
                else if(!strcmp(w[1], "team"))
                {
                    masterclient::statstate::team t;
                    t.index = (int)strtol(w[2], NULL, 10);
                    t.score = (int)strtol(w[3], NULL, 10);
                    simpledecode(name_dec, w[4]);
                    copystring(t.name, name_dec);
                    c.stats.teams.add(t);
                }
                else if(!strcmp(w[1], "player"))
                {
                    masterclient::statstate::player p;
                    simpledecode(name_dec, w[2]);
                    copystring(p.name, name_dec);
                    simpledecode(handle_dec, w[3]);
                    copystring(p.handle, handle_dec);
                    p.score = (int)strtol(w[4], NULL, 10);
                    p.timealive = (int)strtol(w[5], NULL, 10);
                    p.frags = (int)strtol(w[6], NULL, 10);
                    p.deaths = (int)strtol(w[7], NULL, 10);
                    p.wid = (int)strtol(w[8], NULL, 10);
                    c.stats.players.add(p);
                }
                else if(!strcmp(w[1], "weapon"))
                {
                    #define wint(n) ws.n = (int)strtol(w[qidx++], NULL, 10);
                    masterclient::statstate::weaponstats ws;
                    ws.pid = (int)strtol(w[2], NULL, 10);
                    copystring(ws.name, w[3]);
                    int qidx = 4;

                    wint(timewielded);
                    wint(timeloadout);

                    wint(damage1);
                    wint(frags1);
                    wint(hits1);
                    wint(flakhits1);
                    wint(shots1);
                    wint(flakshots1);

                    wint(damage2);
                    wint(frags2);
                    wint(hits2);
                    wint(flakhits2);
                    wint(shots2);
                    wint(flakshots2);

                    c.stats.weapstats.add(ws);
                }
            }
            found = true;
        }
        if(c.isserver || c.isquick)
        {
            if(!strcmp(w[0], "reqauth")) { reqauth(c, uint(atoi(w[1])), w[2], w[3]); found = true; }
            if(!strcmp(w[0], "reqserverauth")) { reqserverauth(c, w[1]); found = true; }
            if(!strcmp(w[0], "confauth")) { confauth(c, uint(atoi(w[1])), w[2]); found = true; }
            if(!strcmp(w[0], "confserverauth")) { confserverauth(c, w[1]); found = true; }
        }
        if(w[0] && *w[0] && !found)
        {
            masteroutf(c, "error \"unknown command %s\"\n", w[0]);
            conoutf("master peer %s (client) sent unknown command: %s",  c.name, w[0]);
        }
        loopi(numargs) DELETEA(w[i]);
    }
    c.inputpos = &c.input[c.inputpos] - p;
    memmove(c.input, p, c.inputpos);
    return c.inputpos < (int)sizeof(c.input);
}

void checkmaster()
{
    if(mastersocket == ENET_SOCKET_NULL || pingsocket == ENET_SOCKET_NULL) return;

    ENetSocketSet readset, writeset;
    ENetSocket maxsock = max(mastersocket, pingsocket);
    ENET_SOCKETSET_EMPTY(readset);
    ENET_SOCKETSET_EMPTY(writeset);
    ENET_SOCKETSET_ADD(readset, mastersocket);
    ENET_SOCKETSET_ADD(readset, pingsocket);
    loopv(masterclients)
    {
        masterclient &c = *masterclients[i];
        if(c.authreqs.length()) purgeauths(c);
        if(c.shouldping && (!c.lastping || ((!c.lastpong || ENET_TIME_GREATER(c.lastping, c.lastpong)) && ENET_TIME_DIFFERENCE(totalmillis, c.lastping) > uint(masterpingdelay))))
        {
            if(c.numpings < masterpingtries)
            {
                static const uchar ping[] = { 1 };
                ENetBuffer buf;
                buf.data = (void *)ping;
                buf.dataLength = sizeof(ping);
                ENetAddress addr = { c.address.host, enet_uint16(c.port+1) };
                c.numpings++;
                c.lastping = totalmillis ? totalmillis : 1;
                enet_socket_send(pingsocket, &addr, &buf, 1);
            }
            else
            {
                c.listserver = c.shouldping = false;
                masteroutf(c, "error \"ping attempts failed (tried %d times on port %d), server will not be listed\"\n", c.numpings, c.port+1);
            }
        }
        if(c.isserver && c.lastcontrol < controlversion)
        {
            loopv(control) if(control[i].type < ipinfo::SYNCTYPES && control[i].flag == ipinfo::LOCAL && control[i].version > c.lastcontrol)
                masteroutf(c, "%s %u %u %s\n", ipinfotypes[control[i].type], control[i].ip, control[i].mask, control[i].reason);
            c.lastcontrol = controlversion;
        }
        if(c.outputpos < c.output.length()) ENET_SOCKETSET_ADD(writeset, c.socket);
        else ENET_SOCKETSET_ADD(readset, c.socket);
        maxsock = max(maxsock, c.socket);
    }
    if(enet_socketset_select(maxsock, &readset, &writeset, 0) <= 0) return;

    if(ENET_SOCKETSET_CHECK(readset, pingsocket)) checkmasterpongs();

    if(ENET_SOCKETSET_CHECK(readset, mastersocket)) for(;;)
    {
        ENetAddress address;
        ENetSocket masterclientsocket = enet_socket_accept(mastersocket, &address);
        if(masterclients.length() >= MASTER_LIMIT || (checkipinfo(control, ipinfo::BAN, address.host) && !checkipinfo(control, ipinfo::EXCEPT, address.host) && !checkipinfo(control, ipinfo::TRUST, address.host)))
        {
            enet_socket_destroy(masterclientsocket);
            break;
        }
        if(masterduplimit && !checkipinfo(control, ipinfo::TRUST, address.host))
        {
            int dups = 0;
            loopv(masterclients) if(masterclients[i]->address.host == address.host) dups++;
            if(dups >= masterduplimit)
            {
                enet_socket_destroy(masterclientsocket);
                break;
            }
        }
        if(masterclientsocket != ENET_SOCKET_NULL)
        {
            masterclient *c = new masterclient;
            c->address = address;
            c->socket = masterclientsocket;
            c->lastactivity = totalmillis ? totalmillis : 1;
            masterclients.add(c);
            if(enet_address_get_host_ip(&c->address, c->name, sizeof(c->name)) < 0) copystring(c->name, "unknown");
            if(verbose) conoutf("master peer %s connected", c->name);
        }
        break;
    }

    loopv(masterclients)
    {
        masterclient &c = *masterclients[i];
        if(c.outputpos < c.output.length() && ENET_SOCKETSET_CHECK(writeset, c.socket))
        {
            ENetBuffer buf;
            buf.data = (void *)&c.output[c.outputpos];
            buf.dataLength = c.output.length()-c.outputpos;
            int res = enet_socket_send(c.socket, NULL, &buf, 1);
            if(res >= 0)
            {
                c.outputpos += res;
                if(c.outputpos >= c.output.length())
                {
                    c.output.setsize(0);
                    c.outputpos = 0;
                    if(c.shouldpurge) { purgemasterclient(i--); continue; }
                }
            }
            else { purgemasterclient(i--); continue; }
        }
        if(ENET_SOCKETSET_CHECK(readset, c.socket))
        {
            ENetBuffer buf;
            buf.data = &c.input[c.inputpos];
            buf.dataLength = sizeof(c.input) - c.inputpos;
            int res = enet_socket_receive(c.socket, NULL, &buf, 1);
            if(res > 0)
            {
                c.inputpos += res;
                c.input[min(c.inputpos, (int)sizeof(c.input)-1)] = '\0';
                if(!checkmasterclientinput(c)) { purgemasterclient(i--); continue; }
            }
            else { purgemasterclient(i--); continue; }
        }
        /* if(c.output.length() > OUTPUT_LIMIT) { purgemasterclient(i--); continue; } */
        if(ENET_TIME_DIFFERENCE(totalmillis, c.lastactivity) >= (c.isserver ? SERVER_TIME : CLIENT_TIME) || (checkipinfo(control, ipinfo::BAN, c.address.host) && !checkipinfo(control, ipinfo::EXCEPT, c.address.host) && !checkipinfo(control, ipinfo::TRUST, c.address.host)))
        {
            purgemasterclient(i--);
            continue;
        }
    }
}

void cleanupmaster()
{
    if(mastersocket != ENET_SOCKET_NULL) enet_socket_destroy(mastersocket);
    closestatsdb();
}

void reloadmaster()
{
    clearauth();
}
