// LEDGE VR — math library (vec2/vec3/vec4, quat, mat4, AABB, helpers)
// Compact, header-only, no dependencies. All mat4 column-major (GL convention).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lv {

struct vec2 { float x=0, y=0;
    vec2(){} vec2(float a,float b):x(a),y(b){}
    vec2 operator+(const vec2&o)const{return vec2(x+o.x,y+o.y);}
    vec2 operator-(const vec2&o)const{return vec2(x-o.x,y-o.y);}
    vec2 operator*(float s)const{return vec2(x*s,y*s);}
};

struct vec3 {
    float x=0, y=0, z=0;
    vec3(){}
    vec3(float a):x(a),y(a),z(a){}
    vec3(float a,float b,float c):x(a),y(b),z(c){}
    vec3 operator+(const vec3&o)const{return vec3(x+o.x,y+o.y,z+o.z);}
    vec3 operator-(const vec3&o)const{return vec3(x-o.x,y-o.y,z-o.z);}
    vec3 operator-()const{return vec3(-x,-y,-z);}
    vec3 operator*(float s)const{return vec3(x*s,y*s,z*s);}
    vec3 operator*(const vec3&o)const{return vec3(x*o.x,y*o.y,z*o.z);}
    vec3& operator+=(const vec3&o){x+=o.x;y+=o.y;z+=o.z;return *this;}
    vec3& operator-=(const vec3&o){x-=o.x;y-=o.y;z-=o.z;return *this;}
    vec3 operator/(float s)const{float inv=1.0f/s;return vec3(x*inv,y*inv,z*inv);}
    vec3& operator*=(float s){x*=s;y*=s;z*=s;return *this;}
    float& operator[](int i){return (&x)[i];}
    const float& operator[](int i)const{return (&x)[i];}
};
inline vec3 operator*(float s, const vec3&o){return o*s;}

inline float dot(const vec3&a,const vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline vec3 cross(const vec3&a,const vec3&b){return vec3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);}
inline float len(const vec3&a){return sqrtf(dot(a,a));}
inline float len(const vec2&a){return sqrtf(a.x*a.x+a.y*a.y);}
inline vec3 norm(const vec3&a){float l=len(a); return l>1e-8f? a*(1.0f/l): vec3(0,1,0);}
inline vec3 mixv(const vec3&a,const vec3&b,float t){return a+(b-a)*t;}

struct vec4 { float x=0,y=0,z=0,w=0;
    vec4(){} vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
    vec4(const vec3&v,float d):x(v.x),y(v.y),z(v.z),w(d){}
};

struct quat {
    float x=0,y=0,z=0,w=1;
    quat(){}
    quat(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
    // From axis (unit) + angle
    static quat axisAngle(const vec3&axis,float ang){
        float s=sinf(ang*0.5f);
        return quat(axis.x*s,axis.y*s,axis.z*s,cosf(ang*0.5f));
    }
    static quat fromYawPitchRoll(float yaw,float pitch,float roll){
        // ZYX order (yaw around Y, pitch around X, roll around Z)
        float cy=cosf(yaw*0.5f), sy=sinf(yaw*0.5f);
        float cp=cosf(pitch*0.5f), sp=sinf(pitch*0.5f);
        float cr=cosf(roll*0.5f), sr=sinf(roll*0.5f);
        return quat(
            sp*cy*cr - cp*sy*sr,
            cp*sy*cr + sp*cy*sr,
            cp*cy*sr - sp*sy*cr,
            cp*cy*cr + sp*sy*sr);
    }
    quat operator*(const quat&o)const{
        return quat(
            w*o.x + x*o.w + y*o.z - z*o.y,
            w*o.y - x*o.z + y*o.w + z*o.x,
            w*o.z + x*o.y - y*o.x + z*o.w,
            w*o.w - x*o.x - y*o.y - z*o.z);
    }
    quat conj()const{return quat(-x,-y,-z,w);}
    vec3 rotate(const vec3&v)const{
        // v' = q * v * q*
        vec3 u(x,y,z), b(cross(u,v));
        return v + (b*2.0f)*w + cross(u,b)*2.0f;
    }
    vec3 rotateInv(const vec3&v)const{ return conj().rotate(v); }
    float pitch()const{
        return atan2f(2.0f*(w*y + z*x), 1.0f - 2.0f*(x*x + y*y)); // X rotation
    }
    float yaw()const{
        return atan2f(2.0f*(w*z + x*y), 1.0f - 2.0f*(y*y + z*z)); // Y rotation
    }
    void normalize(){
        float n=sqrtf(x*x+y*y+z*z+w*w); if(n>1e-8f){x/=n;y/=n;z/=n;w/=n;}
    }
};

struct mat4 {
    float m[16];
    mat4(){ memset(m,0,sizeof(m)); m[0]=m[5]=m[10]=m[15]=1.0f; }
    float* data(){return m;}
    const float* data()const{return m;}
    static mat4 identity(){return mat4();}
    static mat4 translate(const vec3&t){
        mat4 r; r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z; return r;
    }
    static mat4 scale(const vec3&s){
        mat4 r; r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; return r;
    }
    static mat4 rotY(float a){
        mat4 r; float c=cosf(a),s=sinf(a);
        r.m[0]=c; r.m[2]=-s; r.m[8]=s; r.m[10]=c; return r;
    }
    static mat4 rotX(float a){
        mat4 r; float c=cosf(a),s=sinf(a);
        r.m[5]=c; r.m[6]=s; r.m[9]=-s; r.m[10]=c; return r;
    }
    static mat4 rotZ(float a){
        mat4 r; float c=cosf(a),s=sinf(a);
        r.m[0]=c; r.m[1]=s; r.m[4]=-s; r.m[5]=c; return r;
    }
    static mat4 fromQuat(const quat&q){
        mat4 r;
        float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
        float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
        float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
        r.m[0]=1-2*(yy+zz); r.m[1]=2*(xy+wz);   r.m[2]=2*(xz-wy);   r.m[3]=0;
        r.m[4]=2*(xy-wz);   r.m[5]=1-2*(xx+zz); r.m[6]=2*(yz+wx);   r.m[7]=0;
        r.m[8]=2*(xz+wy);   r.m[9]=2*(yz-wx);   r.m[10]=1-2*(xx+yy);r.m[11]=0;
        r.m[12]=0;r.m[13]=0;r.m[14]=0;r.m[15]=1;
        return r;
    }
    static mat4 trs(const vec3&t,const quat&q,const vec3&s){
        return translate(t)*fromQuat(q)*scale(s);
    }
    static mat4 perspective(float fovyRad,float aspect,float zn,float zf){
        mat4 r; float f=1.0f/tanf(fovyRad*0.5f);
        r.m[0]=f/aspect; r.m[5]=f;
        r.m[10]=(zf+zn)/(zn-zf); r.m[11]=-1.0f;
        r.m[14]=2.0f*zf*zn/(zn-zf); r.m[15]=0;
        return r;
    }
    static mat4 ortho(float l,float r_,float b,float t_,float n,float f){
        mat4 m2;
        m2.m[0]=2.0f/(r_-l); m2.m[5]=2.0f/(t_-b); m2.m[10]=-2.0f/(f-n);
        m2.m[12]=-(r_+l)/(r_-l); m2.m[13]=-(t_+b)/(t_-b); m2.m[14]=-(f+n)/(f-n);
        return m2;
    }
    mat4 operator*(const mat4&o)const{
        mat4 r;
        for(int c=0;c<4;c++) for(int r0=0;r0<4;r0++){
            float s=0; for(int k=0;k<4;k++) s+=m[k*4+r0]*o.m[c*4+k];
            r.m[c*4+r0]=s;
        }
        return r;
    }
};

// XrPosef-compatible quaternion used by OpenXR
struct pose {
    quat q;
    vec3 p;
};

} // namespace lv
