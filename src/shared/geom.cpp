#include "cube.h"

void vecfromyawpitch(float yaw, float pitch, int move, int strafe, vec &m)
{
    if(move)
    {
        m.x = move*-sinf(RAD*yaw);
        m.y = move*cosf(RAD*yaw);
    }
    else m.x = m.y = 0;

    if(pitch)
    {
        m.x *= cosf(RAD*pitch);
        m.y *= cosf(RAD*pitch);
        m.z = move*sinf(RAD*pitch);
    }
    else m.z = 0;

    if(strafe)
    {
        m.x += strafe*cosf(RAD*yaw);
        m.y += strafe*sinf(RAD*yaw);
    }

    if(!m.iszero()) m.normalize();
}

void vectoyawpitch(const vec &v, float &yaw, float &pitch)
{
    if(v.iszero()) yaw = pitch = 0;
    else
    {
        yaw = -atan2(v.x, v.y)/RAD;
        pitch = asin(v.z/v.magnitude())/RAD;
    }
}

static inline float det2x2(float a, float b, float c, float d) { return a*d - b*c; }
static inline float det3x3(float a1, float a2, float a3,
                           float b1, float b2, float b3,
                           float c1, float c2, float c3)
{
    return a1 * det2x2(b2, b3, c2, c3)
         - b1 * det2x2(a2, a3, c2, c3)
         + c1 * det2x2(a2, a3, b2, b3);
}

float glmatrixf::determinant() const
{
    float a1 = v[0], a2 = v[1], a3 = v[2], a4 = v[3],
          b1 = v[4], b2 = v[5], b3 = v[6], b4 = v[7],
          c1 = v[8], c2 = v[9], c3 = v[10], c4 = v[11],
          d1 = v[12], d2 = v[13], d3 = v[14], d4 = v[15];

    return a1 * det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4)
         - b1 * det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4)
         + c1 * det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4)
         - d1 * det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4);
}

void glmatrixf::adjoint(const glmatrixf &m)
{
    float a1 = m.v[0], a2 = m.v[1], a3 = m.v[2], a4 = m.v[3],
          b1 = m.v[4], b2 = m.v[5], b3 = m.v[6], b4 = m.v[7],
          c1 = m.v[8], c2 = m.v[9], c3 = m.v[10], c4 = m.v[11],
          d1 = m.v[12], d2 = m.v[13], d3 = m.v[14], d4 = m.v[15];

    v[0]  =  det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4);
    v[1]  = -det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4);
    v[2]  =  det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4);
    v[3]  = -det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4);

    v[4]  = -det3x3(b1, b3, b4, c1, c3, c4, d1, d3, d4);
    v[5]  =  det3x3(a1, a3, a4, c1, c3, c4, d1, d3, d4);
    v[6]  = -det3x3(a1, a3, a4, b1, b3, b4, d1, d3, d4);
    v[7]  =  det3x3(a1, a3, a4, b1, b3, b4, c1, c3, c4);

    v[8]  =  det3x3(b1, b2, b4, c1, c2, c4, d1, d2, d4);
    v[9]  = -det3x3(a1, a2, a4, c1, c2, c4, d1, d2, d4);
    v[10] =  det3x3(a1, a2, a4, b1, b2, b4, d1, d2, d4);
    v[11] = -det3x3(a1, a2, a4, b1, b2, b4, c1, c2, c4);

    v[12] = -det3x3(b1, b2, b3, c1, c2, c3, d1, d2, d3);
    v[13] =  det3x3(a1, a2, a3, c1, c2, c3, d1, d2, d3);
    v[14] = -det3x3(a1, a2, a3, b1, b2, b3, d1, d2, d3);
    v[15] =  det3x3(a1, a2, a3, b1, b2, b3, c1, c2, c3);
}

bool glmatrixf::invert(const glmatrixf &m, float mindet)
{
    float a1 = m.v[0], b1 = m.v[4], c1 = m.v[8], d1 = m.v[12];
    adjoint(m);
    float det = a1*v[0] + b1*v[1] + c1*v[2] + d1*v[3]; // float det = m.determinant();
    if(fabs(det) < mindet) return false;
    float invdet = 1/det;
    loopi(16) v[i] *= invdet;
    return true;
}

bool raysphereintersect(const vec &center, float radius, const vec &o, const vec &ray, float &dist)
{
    vec c(center);
    c.sub(o);
    float v = c.dot(ray),
          inside = radius*radius - c.squaredlen();
    if(inside<0 && v<0) return false;
    float d = inside + v*v;
    if(d<0) return false;
    dist = v - sqrt(d);
    return true;
}

bool rayboxintersect(const vec &b, const vec &s, const vec &o, const vec &ray, float &dist, int &orient)
{
    loop(d, 3) if(ray[d])
    {
        int dc = ray[d]<0 ? 1 : 0;
        float pdist = (b[d]+s[d]*dc - o[d]) / ray[d];
        vec v(ray);
        v.mul(pdist).add(o);
        if(v[R[d]] >= b[R[d]] && v[R[d]] <= b[R[d]]+s[R[d]]
        && v[C[d]] >= b[C[d]] && v[C[d]] <= b[C[d]]+s[C[d]])
        {
            dist = pdist;
            orient = 2*d+dc;
            return true;
        }
    }
    return false;
}

bool linecylinderintersect(const vec &from, const vec &to, const vec &start, const vec &end, float radius, float &dist)
{
    vec d(end), m(from), n(to);
    d.sub(start);
    m.sub(start);
    n.sub(from);
    float md = m.dot(d),
          nd = n.dot(d),
          dd = d.squaredlen();
    if(md < 0 && md + nd < 0) return false;
    if(md > dd && md + nd > dd) return false;
    float nn = n.squaredlen(),
          mn = m.dot(n),
          a = dd*nn - nd*nd,
          k = m.squaredlen() - radius*radius,
          c = dd*k - md*md;
    if(fabs(a) < 0.005f)
    {
        if(c > 0) return false;
        if(md < 0) dist = -mn / nn;
        else if(md > dd) dist = (nd - mn) / nn;
        else dist = 0;
        return true;
    }
    else if(c > 0)
    {
        float b = dd*mn - nd*md,
              discrim = b*b - a*c;
        if(discrim < 0) return false;
        dist = (-b - sqrtf(discrim)) / a;
    }
    else dist = 0;
    float offset = md + dist*nd;
    if(offset < 0)
    {
        if(nd <= 0) return false;
        dist = -md / nd;
        if(k + dist*(2*mn + dist*nn) > 0) return false;
    }
    else if(offset > dd)
    {
        if(nd >= 0) return false;
        dist = (dd - md) / nd;
        if(k + dd - 2*md + dist*(2*(mn-nd) + dist*nn) > 0) return false;
    }
    return dist >= 0 && dist <= 1;
}

vec closestpointcylinder(const vec &center, const vec &start, const vec &end, float radius)
{
    vec dir = vec(end).sub(start), relcenter = vec(center).sub(start);
    float height = relcenter.dot(dir) / dir.squaredlen();
    vec raddir = vec(relcenter).sub(vec(dir).mul(height));
    float radlen = raddir.magnitude();
    if(radlen > radius) raddir.mul(radius/radlen);
    return dir.mul(clamp(height, 0.0f, 1.0f)).add(start).add(raddir);
}

extern const vec2 sincos360[721] =
{
    vec2(1.00000000, 0.00000000), vec2(0.99984770, 0.01745241), vec2(0.99939083, 0.03489950), vec2(0.99862953, 0.05233596), vec2(0.99756405, 0.06975647), vec2(0.99619470, 0.08715574), // 0
    vec2(0.99452190, 0.10452846), vec2(0.99254615, 0.12186934), vec2(0.99026807, 0.13917310), vec2(0.98768834, 0.15643447), vec2(0.98480775, 0.17364818), vec2(0.98162718, 0.19080900), // 6
    vec2(0.97814760, 0.20791169), vec2(0.97437006, 0.22495105), vec2(0.97029573, 0.24192190), vec2(0.96592583, 0.25881905), vec2(0.96126170, 0.27563736), vec2(0.95630476, 0.29237170), // 12
    vec2(0.95105652, 0.30901699), vec2(0.94551858, 0.32556815), vec2(0.93969262, 0.34202014), vec2(0.93358043, 0.35836795), vec2(0.92718385, 0.37460659), vec2(0.92050485, 0.39073113), // 18
    vec2(0.91354546, 0.40673664), vec2(0.90630779, 0.42261826), vec2(0.89879405, 0.43837115), vec2(0.89100652, 0.45399050), vec2(0.88294759, 0.46947156), vec2(0.87461971, 0.48480962), // 24
    vec2(0.86602540, 0.50000000), vec2(0.85716730, 0.51503807), vec2(0.84804810, 0.52991926), vec2(0.83867057, 0.54463904), vec2(0.82903757, 0.55919290), vec2(0.81915204, 0.57357644), // 30
    vec2(0.80901699, 0.58778525), vec2(0.79863551, 0.60181502), vec2(0.78801075, 0.61566148), vec2(0.77714596, 0.62932039), vec2(0.76604444, 0.64278761), vec2(0.75470958, 0.65605903), // 36
    vec2(0.74314483, 0.66913061), vec2(0.73135370, 0.68199836), vec2(0.71933980, 0.69465837), vec2(0.70710678, 0.70710678), vec2(0.69465837, 0.71933980), vec2(0.68199836, 0.73135370), // 42
    vec2(0.66913061, 0.74314483), vec2(0.65605903, 0.75470958), vec2(0.64278761, 0.76604444), vec2(0.62932039, 0.77714596), vec2(0.61566148, 0.78801075), vec2(0.60181502, 0.79863551), // 48
    vec2(0.58778525, 0.80901699), vec2(0.57357644, 0.81915204), vec2(0.55919290, 0.82903757), vec2(0.54463904, 0.83867057), vec2(0.52991926, 0.84804810), vec2(0.51503807, 0.85716730), // 54
    vec2(0.50000000, 0.86602540), vec2(0.48480962, 0.87461971), vec2(0.46947156, 0.88294759), vec2(0.45399050, 0.89100652), vec2(0.43837115, 0.89879405), vec2(0.42261826, 0.90630779), // 60
    vec2(0.40673664, 0.91354546), vec2(0.39073113, 0.92050485), vec2(0.37460659, 0.92718385), vec2(0.35836795, 0.93358043), vec2(0.34202014, 0.93969262), vec2(0.32556815, 0.94551858), // 66
    vec2(0.30901699, 0.95105652), vec2(0.29237170, 0.95630476), vec2(0.27563736, 0.96126170), vec2(0.25881905, 0.96592583), vec2(0.24192190, 0.97029573), vec2(0.22495105, 0.97437006), // 72
    vec2(0.20791169, 0.97814760), vec2(0.19080900, 0.98162718), vec2(0.17364818, 0.98480775), vec2(0.15643447, 0.98768834), vec2(0.13917310, 0.99026807), vec2(0.12186934, 0.99254615), // 78
    vec2(0.10452846, 0.99452190), vec2(0.08715574, 0.99619470), vec2(0.06975647, 0.99756405), vec2(0.05233596, 0.99862953), vec2(0.03489950, 0.99939083), vec2(0.01745241, 0.99984770), // 84
    vec2(0.00000000, 1.00000000), vec2(-0.01745241, 0.99984770), vec2(-0.03489950, 0.99939083), vec2(-0.05233596, 0.99862953), vec2(-0.06975647, 0.99756405), vec2(-0.08715574, 0.99619470), // 90
    vec2(-0.10452846, 0.99452190), vec2(-0.12186934, 0.99254615), vec2(-0.13917310, 0.99026807), vec2(-0.15643447, 0.98768834), vec2(-0.17364818, 0.98480775), vec2(-0.19080900, 0.98162718), // 96
    vec2(-0.20791169, 0.97814760), vec2(-0.22495105, 0.97437006), vec2(-0.24192190, 0.97029573), vec2(-0.25881905, 0.96592583), vec2(-0.27563736, 0.96126170), vec2(-0.29237170, 0.95630476), // 102
    vec2(-0.30901699, 0.95105652), vec2(-0.32556815, 0.94551858), vec2(-0.34202014, 0.93969262), vec2(-0.35836795, 0.93358043), vec2(-0.37460659, 0.92718385), vec2(-0.39073113, 0.92050485), // 108
    vec2(-0.40673664, 0.91354546), vec2(-0.42261826, 0.90630779), vec2(-0.43837115, 0.89879405), vec2(-0.45399050, 0.89100652), vec2(-0.46947156, 0.88294759), vec2(-0.48480962, 0.87461971), // 114
    vec2(-0.50000000, 0.86602540), vec2(-0.51503807, 0.85716730), vec2(-0.52991926, 0.84804810), vec2(-0.54463904, 0.83867057), vec2(-0.55919290, 0.82903757), vec2(-0.57357644, 0.81915204), // 120
    vec2(-0.58778525, 0.80901699), vec2(-0.60181502, 0.79863551), vec2(-0.61566148, 0.78801075), vec2(-0.62932039, 0.77714596), vec2(-0.64278761, 0.76604444), vec2(-0.65605903, 0.75470958), // 126
    vec2(-0.66913061, 0.74314483), vec2(-0.68199836, 0.73135370), vec2(-0.69465837, 0.71933980), vec2(-0.70710678, 0.70710678), vec2(-0.71933980, 0.69465837), vec2(-0.73135370, 0.68199836), // 132
    vec2(-0.74314483, 0.66913061), vec2(-0.75470958, 0.65605903), vec2(-0.76604444, 0.64278761), vec2(-0.77714596, 0.62932039), vec2(-0.78801075, 0.61566148), vec2(-0.79863551, 0.60181502), // 138
    vec2(-0.80901699, 0.58778525), vec2(-0.81915204, 0.57357644), vec2(-0.82903757, 0.55919290), vec2(-0.83867057, 0.54463904), vec2(-0.84804810, 0.52991926), vec2(-0.85716730, 0.51503807), // 144
    vec2(-0.86602540, 0.50000000), vec2(-0.87461971, 0.48480962), vec2(-0.88294759, 0.46947156), vec2(-0.89100652, 0.45399050), vec2(-0.89879405, 0.43837115), vec2(-0.90630779, 0.42261826), // 150
    vec2(-0.91354546, 0.40673664), vec2(-0.92050485, 0.39073113), vec2(-0.92718385, 0.37460659), vec2(-0.93358043, 0.35836795), vec2(-0.93969262, 0.34202014), vec2(-0.94551858, 0.32556815), // 156
    vec2(-0.95105652, 0.30901699), vec2(-0.95630476, 0.29237170), vec2(-0.96126170, 0.27563736), vec2(-0.96592583, 0.25881905), vec2(-0.97029573, 0.24192190), vec2(-0.97437006, 0.22495105), // 162
    vec2(-0.97814760, 0.20791169), vec2(-0.98162718, 0.19080900), vec2(-0.98480775, 0.17364818), vec2(-0.98768834, 0.15643447), vec2(-0.99026807, 0.13917310), vec2(-0.99254615, 0.12186934), // 168
    vec2(-0.99452190, 0.10452846), vec2(-0.99619470, 0.08715574), vec2(-0.99756405, 0.06975647), vec2(-0.99862953, 0.05233596), vec2(-0.99939083, 0.03489950), vec2(-0.99984770, 0.01745241), // 174
    vec2(-1.00000000, 0.00000000), vec2(-0.99984770, -0.01745241), vec2(-0.99939083, -0.03489950), vec2(-0.99862953, -0.05233596), vec2(-0.99756405, -0.06975647), vec2(-0.99619470, -0.08715574), // 180
    vec2(-0.99452190, -0.10452846), vec2(-0.99254615, -0.12186934), vec2(-0.99026807, -0.13917310), vec2(-0.98768834, -0.15643447), vec2(-0.98480775, -0.17364818), vec2(-0.98162718, -0.19080900), // 186
    vec2(-0.97814760, -0.20791169), vec2(-0.97437006, -0.22495105), vec2(-0.97029573, -0.24192190), vec2(-0.96592583, -0.25881905), vec2(-0.96126170, -0.27563736), vec2(-0.95630476, -0.29237170), // 192
    vec2(-0.95105652, -0.30901699), vec2(-0.94551858, -0.32556815), vec2(-0.93969262, -0.34202014), vec2(-0.93358043, -0.35836795), vec2(-0.92718385, -0.37460659), vec2(-0.92050485, -0.39073113), // 198
    vec2(-0.91354546, -0.40673664), vec2(-0.90630779, -0.42261826), vec2(-0.89879405, -0.43837115), vec2(-0.89100652, -0.45399050), vec2(-0.88294759, -0.46947156), vec2(-0.87461971, -0.48480962), // 204
    vec2(-0.86602540, -0.50000000), vec2(-0.85716730, -0.51503807), vec2(-0.84804810, -0.52991926), vec2(-0.83867057, -0.54463904), vec2(-0.82903757, -0.55919290), vec2(-0.81915204, -0.57357644), // 210
    vec2(-0.80901699, -0.58778525), vec2(-0.79863551, -0.60181502), vec2(-0.78801075, -0.61566148), vec2(-0.77714596, -0.62932039), vec2(-0.76604444, -0.64278761), vec2(-0.75470958, -0.65605903), // 216
    vec2(-0.74314483, -0.66913061), vec2(-0.73135370, -0.68199836), vec2(-0.71933980, -0.69465837), vec2(-0.70710678, -0.70710678), vec2(-0.69465837, -0.71933980), vec2(-0.68199836, -0.73135370), // 222
    vec2(-0.66913061, -0.74314483), vec2(-0.65605903, -0.75470958), vec2(-0.64278761, -0.76604444), vec2(-0.62932039, -0.77714596), vec2(-0.61566148, -0.78801075), vec2(-0.60181502, -0.79863551), // 228
    vec2(-0.58778525, -0.80901699), vec2(-0.57357644, -0.81915204), vec2(-0.55919290, -0.82903757), vec2(-0.54463904, -0.83867057), vec2(-0.52991926, -0.84804810), vec2(-0.51503807, -0.85716730), // 234
    vec2(-0.50000000, -0.86602540), vec2(-0.48480962, -0.87461971), vec2(-0.46947156, -0.88294759), vec2(-0.45399050, -0.89100652), vec2(-0.43837115, -0.89879405), vec2(-0.42261826, -0.90630779), // 240
    vec2(-0.40673664, -0.91354546), vec2(-0.39073113, -0.92050485), vec2(-0.37460659, -0.92718385), vec2(-0.35836795, -0.93358043), vec2(-0.34202014, -0.93969262), vec2(-0.32556815, -0.94551858), // 246
    vec2(-0.30901699, -0.95105652), vec2(-0.29237170, -0.95630476), vec2(-0.27563736, -0.96126170), vec2(-0.25881905, -0.96592583), vec2(-0.24192190, -0.97029573), vec2(-0.22495105, -0.97437006), // 252
    vec2(-0.20791169, -0.97814760), vec2(-0.19080900, -0.98162718), vec2(-0.17364818, -0.98480775), vec2(-0.15643447, -0.98768834), vec2(-0.13917310, -0.99026807), vec2(-0.12186934, -0.99254615), // 258
    vec2(-0.10452846, -0.99452190), vec2(-0.08715574, -0.99619470), vec2(-0.06975647, -0.99756405), vec2(-0.05233596, -0.99862953), vec2(-0.03489950, -0.99939083), vec2(-0.01745241, -0.99984770), // 264
    vec2(-0.00000000, -1.00000000), vec2(0.01745241, -0.99984770), vec2(0.03489950, -0.99939083), vec2(0.05233596, -0.99862953), vec2(0.06975647, -0.99756405), vec2(0.08715574, -0.99619470), // 270
    vec2(0.10452846, -0.99452190), vec2(0.12186934, -0.99254615), vec2(0.13917310, -0.99026807), vec2(0.15643447, -0.98768834), vec2(0.17364818, -0.98480775), vec2(0.19080900, -0.98162718), // 276
    vec2(0.20791169, -0.97814760), vec2(0.22495105, -0.97437006), vec2(0.24192190, -0.97029573), vec2(0.25881905, -0.96592583), vec2(0.27563736, -0.96126170), vec2(0.29237170, -0.95630476), // 282
    vec2(0.30901699, -0.95105652), vec2(0.32556815, -0.94551858), vec2(0.34202014, -0.93969262), vec2(0.35836795, -0.93358043), vec2(0.37460659, -0.92718385), vec2(0.39073113, -0.92050485), // 288
    vec2(0.40673664, -0.91354546), vec2(0.42261826, -0.90630779), vec2(0.43837115, -0.89879405), vec2(0.45399050, -0.89100652), vec2(0.46947156, -0.88294759), vec2(0.48480962, -0.87461971), // 294
    vec2(0.50000000, -0.86602540), vec2(0.51503807, -0.85716730), vec2(0.52991926, -0.84804810), vec2(0.54463904, -0.83867057), vec2(0.55919290, -0.82903757), vec2(0.57357644, -0.81915204), // 300
    vec2(0.58778525, -0.80901699), vec2(0.60181502, -0.79863551), vec2(0.61566148, -0.78801075), vec2(0.62932039, -0.77714596), vec2(0.64278761, -0.76604444), vec2(0.65605903, -0.75470958), // 306
    vec2(0.66913061, -0.74314483), vec2(0.68199836, -0.73135370), vec2(0.69465837, -0.71933980), vec2(0.70710678, -0.70710678), vec2(0.71933980, -0.69465837), vec2(0.73135370, -0.68199836), // 312
    vec2(0.74314483, -0.66913061), vec2(0.75470958, -0.65605903), vec2(0.76604444, -0.64278761), vec2(0.77714596, -0.62932039), vec2(0.78801075, -0.61566148), vec2(0.79863551, -0.60181502), // 318
    vec2(0.80901699, -0.58778525), vec2(0.81915204, -0.57357644), vec2(0.82903757, -0.55919290), vec2(0.83867057, -0.54463904), vec2(0.84804810, -0.52991926), vec2(0.85716730, -0.51503807), // 324
    vec2(0.86602540, -0.50000000), vec2(0.87461971, -0.48480962), vec2(0.88294759, -0.46947156), vec2(0.89100652, -0.45399050), vec2(0.89879405, -0.43837115), vec2(0.90630779, -0.42261826), // 330
    vec2(0.91354546, -0.40673664), vec2(0.92050485, -0.39073113), vec2(0.92718385, -0.37460659), vec2(0.93358043, -0.35836795), vec2(0.93969262, -0.34202014), vec2(0.94551858, -0.32556815), // 336
    vec2(0.95105652, -0.30901699), vec2(0.95630476, -0.29237170), vec2(0.96126170, -0.27563736), vec2(0.96592583, -0.25881905), vec2(0.97029573, -0.24192190), vec2(0.97437006, -0.22495105), // 342
    vec2(0.97814760, -0.20791169), vec2(0.98162718, -0.19080900), vec2(0.98480775, -0.17364818), vec2(0.98768834, -0.15643447), vec2(0.99026807, -0.13917310), vec2(0.99254615, -0.12186934), // 348
    vec2(0.99452190, -0.10452846), vec2(0.99619470, -0.08715574), vec2(0.99756405, -0.06975647), vec2(0.99862953, -0.05233596), vec2(0.99939083, -0.03489950), vec2(0.99984770, -0.01745241), // 354
    vec2(1.00000000, 0.00000000), vec2(0.99984770, 0.01745241), vec2(0.99939083, 0.03489950), vec2(0.99862953, 0.05233596), vec2(0.99756405, 0.06975647), vec2(0.99619470, 0.08715574), // 360
    vec2(0.99452190, 0.10452846), vec2(0.99254615, 0.12186934), vec2(0.99026807, 0.13917310), vec2(0.98768834, 0.15643447), vec2(0.98480775, 0.17364818), vec2(0.98162718, 0.19080900), // 366
    vec2(0.97814760, 0.20791169), vec2(0.97437006, 0.22495105), vec2(0.97029573, 0.24192190), vec2(0.96592583, 0.25881905), vec2(0.96126170, 0.27563736), vec2(0.95630476, 0.29237170), // 372
    vec2(0.95105652, 0.30901699), vec2(0.94551858, 0.32556815), vec2(0.93969262, 0.34202014), vec2(0.93358043, 0.35836795), vec2(0.92718385, 0.37460659), vec2(0.92050485, 0.39073113), // 378
    vec2(0.91354546, 0.40673664), vec2(0.90630779, 0.42261826), vec2(0.89879405, 0.43837115), vec2(0.89100652, 0.45399050), vec2(0.88294759, 0.46947156), vec2(0.87461971, 0.48480962), // 384
    vec2(0.86602540, 0.50000000), vec2(0.85716730, 0.51503807), vec2(0.84804810, 0.52991926), vec2(0.83867057, 0.54463904), vec2(0.82903757, 0.55919290), vec2(0.81915204, 0.57357644), // 390
    vec2(0.80901699, 0.58778525), vec2(0.79863551, 0.60181502), vec2(0.78801075, 0.61566148), vec2(0.77714596, 0.62932039), vec2(0.76604444, 0.64278761), vec2(0.75470958, 0.65605903), // 396
    vec2(0.74314483, 0.66913061), vec2(0.73135370, 0.68199836), vec2(0.71933980, 0.69465837), vec2(0.70710678, 0.70710678), vec2(0.69465837, 0.71933980), vec2(0.68199836, 0.73135370), // 402
    vec2(0.66913061, 0.74314483), vec2(0.65605903, 0.75470958), vec2(0.64278761, 0.76604444), vec2(0.62932039, 0.77714596), vec2(0.61566148, 0.78801075), vec2(0.60181502, 0.79863551), // 408
    vec2(0.58778525, 0.80901699), vec2(0.57357644, 0.81915204), vec2(0.55919290, 0.82903757), vec2(0.54463904, 0.83867057), vec2(0.52991926, 0.84804810), vec2(0.51503807, 0.85716730), // 414
    vec2(0.50000000, 0.86602540), vec2(0.48480962, 0.87461971), vec2(0.46947156, 0.88294759), vec2(0.45399050, 0.89100652), vec2(0.43837115, 0.89879405), vec2(0.42261826, 0.90630779), // 420
    vec2(0.40673664, 0.91354546), vec2(0.39073113, 0.92050485), vec2(0.37460659, 0.92718385), vec2(0.35836795, 0.93358043), vec2(0.34202014, 0.93969262), vec2(0.32556815, 0.94551858), // 426
    vec2(0.30901699, 0.95105652), vec2(0.29237170, 0.95630476), vec2(0.27563736, 0.96126170), vec2(0.25881905, 0.96592583), vec2(0.24192190, 0.97029573), vec2(0.22495105, 0.97437006), // 432
    vec2(0.20791169, 0.97814760), vec2(0.19080900, 0.98162718), vec2(0.17364818, 0.98480775), vec2(0.15643447, 0.98768834), vec2(0.13917310, 0.99026807), vec2(0.12186934, 0.99254615), // 438
    vec2(0.10452846, 0.99452190), vec2(0.08715574, 0.99619470), vec2(0.06975647, 0.99756405), vec2(0.05233596, 0.99862953), vec2(0.03489950, 0.99939083), vec2(0.01745241, 0.99984770), // 444
    vec2(0.00000000, 1.00000000), vec2(-0.01745241, 0.99984770), vec2(-0.03489950, 0.99939083), vec2(-0.05233596, 0.99862953), vec2(-0.06975647, 0.99756405), vec2(-0.08715574, 0.99619470), // 450
    vec2(-0.10452846, 0.99452190), vec2(-0.12186934, 0.99254615), vec2(-0.13917310, 0.99026807), vec2(-0.15643447, 0.98768834), vec2(-0.17364818, 0.98480775), vec2(-0.19080900, 0.98162718), // 456
    vec2(-0.20791169, 0.97814760), vec2(-0.22495105, 0.97437006), vec2(-0.24192190, 0.97029573), vec2(-0.25881905, 0.96592583), vec2(-0.27563736, 0.96126170), vec2(-0.29237170, 0.95630476), // 462
    vec2(-0.30901699, 0.95105652), vec2(-0.32556815, 0.94551858), vec2(-0.34202014, 0.93969262), vec2(-0.35836795, 0.93358043), vec2(-0.37460659, 0.92718385), vec2(-0.39073113, 0.92050485), // 468
    vec2(-0.40673664, 0.91354546), vec2(-0.42261826, 0.90630779), vec2(-0.43837115, 0.89879405), vec2(-0.45399050, 0.89100652), vec2(-0.46947156, 0.88294759), vec2(-0.48480962, 0.87461971), // 474
    vec2(-0.50000000, 0.86602540), vec2(-0.51503807, 0.85716730), vec2(-0.52991926, 0.84804810), vec2(-0.54463904, 0.83867057), vec2(-0.55919290, 0.82903757), vec2(-0.57357644, 0.81915204), // 480
    vec2(-0.58778525, 0.80901699), vec2(-0.60181502, 0.79863551), vec2(-0.61566148, 0.78801075), vec2(-0.62932039, 0.77714596), vec2(-0.64278761, 0.76604444), vec2(-0.65605903, 0.75470958), // 486
    vec2(-0.66913061, 0.74314483), vec2(-0.68199836, 0.73135370), vec2(-0.69465837, 0.71933980), vec2(-0.70710678, 0.70710678), vec2(-0.71933980, 0.69465837), vec2(-0.73135370, 0.68199836), // 492
    vec2(-0.74314483, 0.66913061), vec2(-0.75470958, 0.65605903), vec2(-0.76604444, 0.64278761), vec2(-0.77714596, 0.62932039), vec2(-0.78801075, 0.61566148), vec2(-0.79863551, 0.60181502), // 498
    vec2(-0.80901699, 0.58778525), vec2(-0.81915204, 0.57357644), vec2(-0.82903757, 0.55919290), vec2(-0.83867057, 0.54463904), vec2(-0.84804810, 0.52991926), vec2(-0.85716730, 0.51503807), // 504
    vec2(-0.86602540, 0.50000000), vec2(-0.87461971, 0.48480962), vec2(-0.88294759, 0.46947156), vec2(-0.89100652, 0.45399050), vec2(-0.89879405, 0.43837115), vec2(-0.90630779, 0.42261826), // 510
    vec2(-0.91354546, 0.40673664), vec2(-0.92050485, 0.39073113), vec2(-0.92718385, 0.37460659), vec2(-0.93358043, 0.35836795), vec2(-0.93969262, 0.34202014), vec2(-0.94551858, 0.32556815), // 516
    vec2(-0.95105652, 0.30901699), vec2(-0.95630476, 0.29237170), vec2(-0.96126170, 0.27563736), vec2(-0.96592583, 0.25881905), vec2(-0.97029573, 0.24192190), vec2(-0.97437006, 0.22495105), // 522
    vec2(-0.97814760, 0.20791169), vec2(-0.98162718, 0.19080900), vec2(-0.98480775, 0.17364818), vec2(-0.98768834, 0.15643447), vec2(-0.99026807, 0.13917310), vec2(-0.99254615, 0.12186934), // 528
    vec2(-0.99452190, 0.10452846), vec2(-0.99619470, 0.08715574), vec2(-0.99756405, 0.06975647), vec2(-0.99862953, 0.05233596), vec2(-0.99939083, 0.03489950), vec2(-0.99984770, 0.01745241), // 534
    vec2(-1.00000000, 0.00000000), vec2(-0.99984770, -0.01745241), vec2(-0.99939083, -0.03489950), vec2(-0.99862953, -0.05233596), vec2(-0.99756405, -0.06975647), vec2(-0.99619470, -0.08715574), // 540
    vec2(-0.99452190, -0.10452846), vec2(-0.99254615, -0.12186934), vec2(-0.99026807, -0.13917310), vec2(-0.98768834, -0.15643447), vec2(-0.98480775, -0.17364818), vec2(-0.98162718, -0.19080900), // 546
    vec2(-0.97814760, -0.20791169), vec2(-0.97437006, -0.22495105), vec2(-0.97029573, -0.24192190), vec2(-0.96592583, -0.25881905), vec2(-0.96126170, -0.27563736), vec2(-0.95630476, -0.29237170), // 552
    vec2(-0.95105652, -0.30901699), vec2(-0.94551858, -0.32556815), vec2(-0.93969262, -0.34202014), vec2(-0.93358043, -0.35836795), vec2(-0.92718385, -0.37460659), vec2(-0.92050485, -0.39073113), // 558
    vec2(-0.91354546, -0.40673664), vec2(-0.90630779, -0.42261826), vec2(-0.89879405, -0.43837115), vec2(-0.89100652, -0.45399050), vec2(-0.88294759, -0.46947156), vec2(-0.87461971, -0.48480962), // 564
    vec2(-0.86602540, -0.50000000), vec2(-0.85716730, -0.51503807), vec2(-0.84804810, -0.52991926), vec2(-0.83867057, -0.54463904), vec2(-0.82903757, -0.55919290), vec2(-0.81915204, -0.57357644), // 570
    vec2(-0.80901699, -0.58778525), vec2(-0.79863551, -0.60181502), vec2(-0.78801075, -0.61566148), vec2(-0.77714596, -0.62932039), vec2(-0.76604444, -0.64278761), vec2(-0.75470958, -0.65605903), // 576
    vec2(-0.74314483, -0.66913061), vec2(-0.73135370, -0.68199836), vec2(-0.71933980, -0.69465837), vec2(-0.70710678, -0.70710678), vec2(-0.69465837, -0.71933980), vec2(-0.68199836, -0.73135370), // 582
    vec2(-0.66913061, -0.74314483), vec2(-0.65605903, -0.75470958), vec2(-0.64278761, -0.76604444), vec2(-0.62932039, -0.77714596), vec2(-0.61566148, -0.78801075), vec2(-0.60181502, -0.79863551), // 588
    vec2(-0.58778525, -0.80901699), vec2(-0.57357644, -0.81915204), vec2(-0.55919290, -0.82903757), vec2(-0.54463904, -0.83867057), vec2(-0.52991926, -0.84804810), vec2(-0.51503807, -0.85716730), // 594
    vec2(-0.50000000, -0.86602540), vec2(-0.48480962, -0.87461971), vec2(-0.46947156, -0.88294759), vec2(-0.45399050, -0.89100652), vec2(-0.43837115, -0.89879405), vec2(-0.42261826, -0.90630779), // 600
    vec2(-0.40673664, -0.91354546), vec2(-0.39073113, -0.92050485), vec2(-0.37460659, -0.92718385), vec2(-0.35836795, -0.93358043), vec2(-0.34202014, -0.93969262), vec2(-0.32556815, -0.94551858), // 606
    vec2(-0.30901699, -0.95105652), vec2(-0.29237170, -0.95630476), vec2(-0.27563736, -0.96126170), vec2(-0.25881905, -0.96592583), vec2(-0.24192190, -0.97029573), vec2(-0.22495105, -0.97437006), // 612
    vec2(-0.20791169, -0.97814760), vec2(-0.19080900, -0.98162718), vec2(-0.17364818, -0.98480775), vec2(-0.15643447, -0.98768834), vec2(-0.13917310, -0.99026807), vec2(-0.12186934, -0.99254615), // 618
    vec2(-0.10452846, -0.99452190), vec2(-0.08715574, -0.99619470), vec2(-0.06975647, -0.99756405), vec2(-0.05233596, -0.99862953), vec2(-0.03489950, -0.99939083), vec2(-0.01745241, -0.99984770), // 624
    vec2(-0.00000000, -1.00000000), vec2(0.01745241, -0.99984770), vec2(0.03489950, -0.99939083), vec2(0.05233596, -0.99862953), vec2(0.06975647, -0.99756405), vec2(0.08715574, -0.99619470), // 630
    vec2(0.10452846, -0.99452190), vec2(0.12186934, -0.99254615), vec2(0.13917310, -0.99026807), vec2(0.15643447, -0.98768834), vec2(0.17364818, -0.98480775), vec2(0.19080900, -0.98162718), // 636
    vec2(0.20791169, -0.97814760), vec2(0.22495105, -0.97437006), vec2(0.24192190, -0.97029573), vec2(0.25881905, -0.96592583), vec2(0.27563736, -0.96126170), vec2(0.29237170, -0.95630476), // 642
    vec2(0.30901699, -0.95105652), vec2(0.32556815, -0.94551858), vec2(0.34202014, -0.93969262), vec2(0.35836795, -0.93358043), vec2(0.37460659, -0.92718385), vec2(0.39073113, -0.92050485), // 648
    vec2(0.40673664, -0.91354546), vec2(0.42261826, -0.90630779), vec2(0.43837115, -0.89879405), vec2(0.45399050, -0.89100652), vec2(0.46947156, -0.88294759), vec2(0.48480962, -0.87461971), // 654
    vec2(0.50000000, -0.86602540), vec2(0.51503807, -0.85716730), vec2(0.52991926, -0.84804810), vec2(0.54463904, -0.83867057), vec2(0.55919290, -0.82903757), vec2(0.57357644, -0.81915204), // 660
    vec2(0.58778525, -0.80901699), vec2(0.60181502, -0.79863551), vec2(0.61566148, -0.78801075), vec2(0.62932039, -0.77714596), vec2(0.64278761, -0.76604444), vec2(0.65605903, -0.75470958), // 666
    vec2(0.66913061, -0.74314483), vec2(0.68199836, -0.73135370), vec2(0.69465837, -0.71933980), vec2(0.70710678, -0.70710678), vec2(0.71933980, -0.69465837), vec2(0.73135370, -0.68199836), // 672
    vec2(0.74314483, -0.66913061), vec2(0.75470958, -0.65605903), vec2(0.76604444, -0.64278761), vec2(0.77714596, -0.62932039), vec2(0.78801075, -0.61566148), vec2(0.79863551, -0.60181502), // 678
    vec2(0.80901699, -0.58778525), vec2(0.81915204, -0.57357644), vec2(0.82903757, -0.55919290), vec2(0.83867057, -0.54463904), vec2(0.84804810, -0.52991926), vec2(0.85716730, -0.51503807), // 684
    vec2(0.86602540, -0.50000000), vec2(0.87461971, -0.48480962), vec2(0.88294759, -0.46947156), vec2(0.89100652, -0.45399050), vec2(0.89879405, -0.43837115), vec2(0.90630779, -0.42261826), // 690
    vec2(0.91354546, -0.40673664), vec2(0.92050485, -0.39073113), vec2(0.92718385, -0.37460659), vec2(0.93358043, -0.35836795), vec2(0.93969262, -0.34202014), vec2(0.94551858, -0.32556815), // 696
    vec2(0.95105652, -0.30901699), vec2(0.95630476, -0.29237170), vec2(0.96126170, -0.27563736), vec2(0.96592583, -0.25881905), vec2(0.97029573, -0.24192190), vec2(0.97437006, -0.22495105), // 702
    vec2(0.97814760, -0.20791169), vec2(0.98162718, -0.19080900), vec2(0.98480775, -0.17364818), vec2(0.98768834, -0.15643447), vec2(0.99026807, -0.13917310), vec2(0.99254615, -0.12186934), // 708
    vec2(0.99452190, -0.10452846), vec2(0.99619470, -0.08715574), vec2(0.99756405, -0.06975647), vec2(0.99862953, -0.05233596), vec2(0.99939083, -0.03489950), vec2(0.99984770, -0.01745241), // 714
    vec2(1.00000000, 0.00000000) // 720
};

