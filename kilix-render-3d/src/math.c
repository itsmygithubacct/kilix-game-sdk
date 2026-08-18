#include "kr3d.h"
#include <math.h>

kr3d_vec3 kr3d_vec3_add(kr3d_vec3 a, kr3d_vec3 b){return (kr3d_vec3){a.x+b.x,a.y+b.y,a.z+b.z};}
kr3d_vec3 kr3d_vec3_sub(kr3d_vec3 a, kr3d_vec3 b){return (kr3d_vec3){a.x-b.x,a.y-b.y,a.z-b.z};}
kr3d_vec3 kr3d_vec3_scale(kr3d_vec3 v,float s){return (kr3d_vec3){v.x*s,v.y*s,v.z*s};}
float kr3d_vec3_dot(kr3d_vec3 a,kr3d_vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
kr3d_vec3 kr3d_vec3_cross(kr3d_vec3 a,kr3d_vec3 b){return (kr3d_vec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool kr3d_vec3_normalize(kr3d_vec3 v,kr3d_vec3*out){float n=kr3d_vec3_dot(v,v);if(!out||!isfinite(n)||n<=1e-20f)return false;*out=kr3d_vec3_scale(v,1.0f/sqrtf(n));return true;}
kr3d_quat kr3d_quat_identity(void){return (kr3d_quat){0,0,0,1};}
bool kr3d_quat_normalize(kr3d_quat q,kr3d_quat*out){float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;if(!out||!isfinite(n)||n<=1e-20f)return false;n=1.0f/sqrtf(n);*out=(kr3d_quat){q.x*n,q.y*n,q.z*n,q.w*n};return true;}
bool kr3d_quat_axis_angle(kr3d_vec3 a,float r,kr3d_quat*out){if(!isfinite(r)||!kr3d_vec3_normalize(a,&a)||!out)return false;float s=sinf(r*.5f);*out=(kr3d_quat){a.x*s,a.y*s,a.z*s,cosf(r*.5f)};return true;}
kr3d_quat kr3d_quat_mul(kr3d_quat a,kr3d_quat b){kr3d_quat q={a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};kr3d_quat_normalize(q,&q);return q;}
kr3d_quat kr3d_quat_nlerp(kr3d_quat a,kr3d_quat b,float t){if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0){b.x=-b.x;b.y=-b.y;b.z=-b.z;b.w=-b.w;}kr3d_quat q={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};if(!kr3d_quat_normalize(q,&q))q=kr3d_quat_identity();return q;}
kr3d_mat4 kr3d_mat4_identity(void){kr3d_mat4 r={{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};return r;}
kr3d_mat4 kr3d_mat4_mul(kr3d_mat4 a,kr3d_mat4 b){kr3d_mat4 r={0};for(int c=0;c<4;c++)for(int y=0;y<4;y++)for(int k=0;k<4;k++)r.m[c*4+y]+=a.m[k*4+y]*b.m[c*4+k];return r;}
kr3d_mat4 kr3d_mat4_translate(kr3d_vec3 v){kr3d_mat4 r=kr3d_mat4_identity();r.m[12]=v.x;r.m[13]=v.y;r.m[14]=v.z;return r;}
kr3d_mat4 kr3d_mat4_scale(kr3d_vec3 v){kr3d_mat4 r=kr3d_mat4_identity();r.m[0]=v.x;r.m[5]=v.y;r.m[10]=v.z;return r;}
kr3d_mat4 kr3d_mat4_from_quat(kr3d_quat q){if(!kr3d_quat_normalize(q,&q))return kr3d_mat4_identity();float x=q.x,y=q.y,z=q.z,w=q.w;kr3d_mat4 r=kr3d_mat4_identity();r.m[0]=1-2*y*y-2*z*z;r.m[1]=2*x*y+2*w*z;r.m[2]=2*x*z-2*w*y;r.m[4]=2*x*y-2*w*z;r.m[5]=1-2*x*x-2*z*z;r.m[6]=2*y*z+2*w*x;r.m[8]=2*x*z+2*w*y;r.m[9]=2*y*z-2*w*x;r.m[10]=1-2*x*x-2*y*y;return r;}
bool kr3d_mat4_look_at(kr3d_vec3 e,kr3d_vec3 c,kr3d_vec3 up,kr3d_mat4*out){kr3d_vec3 f=kr3d_vec3_sub(c,e),s,u;if(!out||!kr3d_vec3_normalize(f,&f)||!kr3d_vec3_normalize(kr3d_vec3_cross(f,up),&s))return false;u=kr3d_vec3_cross(s,f);*out=(kr3d_mat4){{s.x,u.x,-f.x,0,s.y,u.y,-f.y,0,s.z,u.z,-f.z,0,-kr3d_vec3_dot(s,e),-kr3d_vec3_dot(u,e),kr3d_vec3_dot(f,e),1}};return true;}
bool kr3d_mat4_perspective(float f,float a,float n,float z,kr3d_mat4*out){if(!out||!isfinite(f)||!isfinite(a)||!isfinite(n)||!isfinite(z)||f<=0||f>=3.1415926f||a<=0||n<=0||z<=n)return false;float q=1/tanf(f*.5f);*out=(kr3d_mat4){{q/a,0,0,0,0,q,0,0,0,0,(z+n)/(n-z),-1,0,0,(2*z*n)/(n-z),0}};return true;}
bool kr3d_mat4_transform_point(kr3d_mat4 m,kr3d_vec3 p,kr3d_vec3*out){float w=m.m[3]*p.x+m.m[7]*p.y+m.m[11]*p.z+m.m[15];if(!out||!isfinite(w)||fabsf(w)<1e-20f)return false;out->x=(m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z+m.m[12])/w;out->y=(m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z+m.m[13])/w;out->z=(m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z+m.m[14])/w;return isfinite(out->x)&&isfinite(out->y)&&isfinite(out->z);}
kr3d_vec3 kr3d_mat4_transform_vector(kr3d_mat4 m,kr3d_vec3 v){return(kr3d_vec3){m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z,m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z,m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z};}
bool kr3d_frustum_from_matrix(kr3d_mat4 m,kr3d_frustum*out){if(!out)return false;const int s[6][2]={{3,0},{3,0},{3,1},{3,1},{3,2},{3,2}};const float sg[6]={1,-1,1,-1,1,-1};for(int i=0;i<6;i++){int a=s[i][0],b=s[i][1];kr3d_plane*p=&out->planes[i];p->normal=(kr3d_vec3){m.m[a]+sg[i]*m.m[b],m.m[4+a]+sg[i]*m.m[4+b],m.m[8+a]+sg[i]*m.m[8+b]};p->d=m.m[12+a]+sg[i]*m.m[12+b];float l=sqrtf(kr3d_vec3_dot(p->normal,p->normal));if(!isfinite(l)||l<1e-20f)return false;p->normal=kr3d_vec3_scale(p->normal,1/l);p->d/=l;}return true;}
bool kr3d_frustum_contains_sphere(const kr3d_frustum*f,kr3d_vec3 c,float r){if(!f||r<0||!isfinite(r))return false;for(int i=0;i<6;i++)if(kr3d_vec3_dot(f->planes[i].normal,c)+f->planes[i].d < -r)return false;return true;}
bool kr3d_frustum_intersects_aabb(const kr3d_frustum*f,kr3d_aabb b){if(!f)return false;for(int i=0;i<6;i++){kr3d_vec3 n=f->planes[i].normal,p={n.x>=0?b.max.x:b.min.x,n.y>=0?b.max.y:b.min.y,n.z>=0?b.max.z:b.min.z};if(kr3d_vec3_dot(n,p)+f->planes[i].d<0)return false;}return true;}
