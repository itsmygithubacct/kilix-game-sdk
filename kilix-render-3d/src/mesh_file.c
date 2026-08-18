#include "kr3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { VERTEX_WIRE_SIZE = 36, SHA_BLOCK_SIZE = 64 };
enum { OFF_HEADER_SIZE=8, OFF_FLAGS=12, OFF_VERTEX_COUNT=16, OFF_INDEX_COUNT=20,
       OFF_MATERIAL_COUNT=24, OFF_BOUNDS_MIN=28, OFF_BOUNDS_MAX=40,
       OFF_VERTEX_STRIDE=52, OFF_VERTEX_BYTES=56, OFF_INDEX_BYTES=64,
       OFF_SHA256=72 };

typedef struct { uint32_t h[8]; uint64_t bits; uint8_t block[64]; size_t used; } sha256;

static void fail(kr3d_error *e, kr3d_error_code code, const char *message)
{ if(e){e->code=code;(void)strncpy(e->message,message,sizeof(e->message)-1u);e->message[sizeof(e->message)-1u]='\0';} }
static uint32_t r32(const uint8_t*p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t r64(const uint8_t*p)
{return (uint64_t)r32(p)|(uint64_t)r32(p+4)<<32;}
static void w32(uint8_t*p,uint32_t v)
{p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void w64(uint8_t*p,uint64_t v){w32(p,(uint32_t)v);w32(p+4,(uint32_t)(v>>32));}
static float rf(const uint8_t*p){uint32_t u=r32(p);float f;memcpy(&f,&u,sizeof f);return f;}
static void wf(uint8_t*p,float f){uint32_t u;memcpy(&u,&f,sizeof u);w32(p,u);}
static uint32_t rr(uint32_t x,unsigned n){return(x>>n)|(x<<(32u-n));}
static void sha_block(sha256*s,const uint8_t*p)
{static const uint32_t k[64]={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
 uint32_t w[64],a,b,c,d,e,f,g,h;unsigned i;
 for(i=0;i<16;i++)w[i]=(uint32_t)p[i*4]<<24|(uint32_t)p[i*4+1]<<16|(uint32_t)p[i*4+2]<<8|p[i*4+3];
 for(;i<64;i++){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(rr(x,7)^rr(x,18)^(x>>3))+w[i-7]+(rr(y,17)^rr(y,19)^(y>>10));}
 a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
 for(i=0;i<64;i++){uint32_t t1=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void sha_init(sha256*s)
{static const uint32_t iv[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};memset(s,0,sizeof *s);memcpy(s->h,iv,sizeof iv);}
static void sha_add(sha256*s,const uint8_t*p,size_t n)
{s->bits+=(uint64_t)n*8u;while(n){size_t take=SHA_BLOCK_SIZE-s->used;if(take>n)take=n;memcpy(s->block+s->used,p,take);s->used+=take;p+=take;n-=take;if(s->used==SHA_BLOCK_SIZE){sha_block(s,s->block);s->used=0;}}}
static void sha_end(sha256*s,uint8_t out[32])
{uint64_t bits=s->bits;unsigned i;s->block[s->used++]=0x80;if(s->used>56){memset(s->block+s->used,0,64-s->used);sha_block(s,s->block);s->used=0;}memset(s->block+s->used,0,56-s->used);for(i=0;i<8;i++)s->block[63-i]=(uint8_t)(bits>>(i*8));sha_block(s,s->block);for(i=0;i<8;i++){out[i*4]=(uint8_t)(s->h[i]>>24);out[i*4+1]=(uint8_t)(s->h[i]>>16);out[i*4+2]=(uint8_t)(s->h[i]>>8);out[i*4+3]=(uint8_t)s->h[i];}}
static void digest(const uint8_t*p,size_t n,uint8_t out[32]){sha256 s;sha_init(&s);sha_add(&s,p,n);sha_end(&s,out);}
static bool valid_vertex(const kr3d_vertex*v)
{float n2=v->normal[0]*v->normal[0]+v->normal[1]*v->normal[1]+v->normal[2]*v->normal[2];unsigned i;for(i=0;i<3;i++)if(!isfinite(v->position[i])||!isfinite(v->normal[i]))return false;for(i=0;i<2;i++)if(!isfinite(v->uv[i]))return false;return n2>.25f&&n2<4.0f;}

void kr3d_mesh_data_release(kr3d_mesh_data*d){if(d){free(d->vertices);free(d->indices);memset(d,0,sizeof *d);}}
void kr3d_mesh_file_bytes_free(void*p){free(p);}

bool kr3d_mesh_file_decode(const void*bytes,size_t size,const kr3d_mesh_file_limits*l,kr3d_mesh_data*out,kr3d_error*e)
{const uint8_t*p=bytes;uint32_t nv,ni,nm;uint64_t vb,ib;size_t payload;uint8_t sum[32];kr3d_mesh_data d={0};uint32_t i,j;float declared_min[3],declared_max[3],actual_min[3],actual_max[3];
 if(!p||!l||!out||l->struct_size!=sizeof *l){fail(e,KR3D_ERROR_ARGUMENT,"invalid mesh file arguments");return false;}memset(out,0,sizeof *out);
 if(size<KR3D_MESH_FILE_HEADER_SIZE||size>l->max_file_bytes){fail(e,KR3D_ERROR_LIMIT,"mesh file size outside limits");return false;}
 if(memcmp(p,KR3D_MESH_FILE_MAGIC,8)||r32(p+OFF_HEADER_SIZE)!=KR3D_MESH_FILE_HEADER_SIZE||r32(p+OFF_FLAGS)!=0||r32(p+OFF_VERTEX_STRIDE)!=VERTEX_WIRE_SIZE){fail(e,KR3D_ERROR_UNSUPPORTED,"unsupported mesh header");return false;}
 nv=r32(p+OFF_VERTEX_COUNT);ni=r32(p+OFF_INDEX_COUNT);nm=r32(p+OFF_MATERIAL_COUNT);vb=r64(p+OFF_VERTEX_BYTES);ib=r64(p+OFF_INDEX_BYTES);
 if(!nv||!ni||ni%3u||!nm||nv>l->max_vertices||ni>l->max_indices||nm>l->max_material_slots||vb!=(uint64_t)nv*VERTEX_WIRE_SIZE||ib!=(uint64_t)ni*4u||vb>SIZE_MAX-ib){fail(e,KR3D_ERROR_LIMIT,"mesh counts outside limits");return false;}
 payload=(size_t)(vb+ib);if(payload!=size-KR3D_MESH_FILE_HEADER_SIZE){fail(e,KR3D_ERROR_ARGUMENT,"mesh payload length mismatch");return false;}digest(p+KR3D_MESH_FILE_HEADER_SIZE,payload,sum);if(memcmp(sum,p+OFF_SHA256,32)){fail(e,KR3D_ERROR_ARGUMENT,"mesh payload checksum mismatch");return false;}
 d.vertices=calloc(nv,sizeof *d.vertices);d.indices=malloc((size_t)ni*sizeof *d.indices);if(!d.vertices||!d.indices){kr3d_mesh_data_release(&d);fail(e,KR3D_ERROR_MEMORY,"mesh allocation failed");return false;}d.vertex_count=nv;d.index_count=ni;d.material_slot_count=nm;
 for(i=0;i<nv;i++){const uint8_t*q=p+KR3D_MESH_FILE_HEADER_SIZE+(size_t)i*VERTEX_WIRE_SIZE;for(j=0;j<3;j++)d.vertices[i].position[j]=rf(q+j*4u);for(j=0;j<3;j++)d.vertices[i].normal[j]=rf(q+12u+j*4u);for(j=0;j<2;j++)d.vertices[i].uv[j]=rf(q+24u+j*4u);d.vertices[i].color_rgba=r32(q+32);if(!valid_vertex(&d.vertices[i])){kr3d_mesh_data_release(&d);fail(e,KR3D_ERROR_ARGUMENT,"invalid mesh vertex");return false;}for(j=0;j<3;j++){if(i==0||d.vertices[i].position[j]<actual_min[j])actual_min[j]=d.vertices[i].position[j];if(i==0||d.vertices[i].position[j]>actual_max[j])actual_max[j]=d.vertices[i].position[j];}}
 for(i=0;i<ni;i++){d.indices[i]=r32(p+KR3D_MESH_FILE_HEADER_SIZE+(size_t)vb+(size_t)i*4u);if(d.indices[i]>=nv){kr3d_mesh_data_release(&d);fail(e,KR3D_ERROR_ARGUMENT,"mesh index out of range");return false;}}
 for(j=0;j<3;j++){declared_min[j]=rf(p+OFF_BOUNDS_MIN+j*4u);declared_max[j]=rf(p+OFF_BOUNDS_MAX+j*4u);if(!isfinite(declared_min[j])||!isfinite(declared_max[j])||declared_min[j]!=actual_min[j]||declared_max[j]!=actual_max[j]){kr3d_mesh_data_release(&d);fail(e,KR3D_ERROR_ARGUMENT,"invalid mesh bounds");return false;}}
 d.bounds.min=(kr3d_vec3){declared_min[0],declared_min[1],declared_min[2]};d.bounds.max=(kr3d_vec3){declared_max[0],declared_max[1],declared_max[2]};
 *out=d;if(e){e->code=KR3D_OK;e->message[0]='\0';}return true;
}

bool kr3d_mesh_file_encode(const kr3d_mesh_desc*m,uint32_t slots,void**out_bytes,size_t*out_size,kr3d_error*e)
{uint8_t*p;size_t vb,ib,total;uint32_t i,j;float minv[3],maxv[3];
 if(!m||m->struct_size!=sizeof *m||!m->vertices||!m->indices||!m->vertex_count||m->vertex_count>UINT32_MAX||!m->index_count||m->index_count>UINT32_MAX||m->index_count%3u||!slots||!out_bytes||!out_size){fail(e,KR3D_ERROR_ARGUMENT,"invalid mesh encode arguments");return false;}*out_bytes=NULL;*out_size=0;
 if(m->vertex_count>(SIZE_MAX-KR3D_MESH_FILE_HEADER_SIZE)/VERTEX_WIRE_SIZE){fail(e,KR3D_ERROR_LIMIT,"mesh too large");return false;}vb=m->vertex_count*VERTEX_WIRE_SIZE;ib=m->index_count*4u;if(ib>SIZE_MAX-KR3D_MESH_FILE_HEADER_SIZE-vb){fail(e,KR3D_ERROR_LIMIT,"mesh too large");return false;}total=KR3D_MESH_FILE_HEADER_SIZE+vb+ib;p=calloc(1,total);if(!p){fail(e,KR3D_ERROR_MEMORY,"mesh allocation failed");return false;}
 for(j=0;j<3;j++)minv[j]=maxv[j]=m->vertices[0].position[j];
 for(i=0;i<m->vertex_count;i++){const kr3d_vertex*v=&m->vertices[i];uint8_t*q=p+KR3D_MESH_FILE_HEADER_SIZE+(size_t)i*VERTEX_WIRE_SIZE;if(!valid_vertex(v)){free(p);fail(e,KR3D_ERROR_ARGUMENT,"invalid mesh vertex");return false;}for(j=0;j<3;j++){wf(q+j*4u,v->position[j]);wf(q+12u+j*4u,v->normal[j]);if(v->position[j]<minv[j])minv[j]=v->position[j];if(v->position[j]>maxv[j])maxv[j]=v->position[j];}for(j=0;j<2;j++)wf(q+24u+j*4u,v->uv[j]);w32(q+32,v->color_rgba);}
 for(i=0;i<m->index_count;i++){if(m->indices[i]>=m->vertex_count){free(p);fail(e,KR3D_ERROR_ARGUMENT,"mesh index out of range");return false;}w32(p+KR3D_MESH_FILE_HEADER_SIZE+vb+(size_t)i*4u,m->indices[i]);}
 memcpy(p,KR3D_MESH_FILE_MAGIC,8);w32(p+OFF_HEADER_SIZE,KR3D_MESH_FILE_HEADER_SIZE);w32(p+OFF_VERTEX_COUNT,(uint32_t)m->vertex_count);w32(p+OFF_INDEX_COUNT,(uint32_t)m->index_count);w32(p+OFF_MATERIAL_COUNT,slots);for(j=0;j<3;j++){wf(p+OFF_BOUNDS_MIN+j*4u,minv[j]);wf(p+OFF_BOUNDS_MAX+j*4u,maxv[j]);}w32(p+OFF_VERTEX_STRIDE,VERTEX_WIRE_SIZE);w64(p+OFF_VERTEX_BYTES,vb);w64(p+OFF_INDEX_BYTES,ib);digest(p+KR3D_MESH_FILE_HEADER_SIZE,vb+ib,p+OFF_SHA256);
 *out_bytes=p;*out_size=total;if(e){e->code=KR3D_OK;e->message[0]='\0';}return true;
}
