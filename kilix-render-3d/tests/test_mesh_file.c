#include "kr3d.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t expected_sha[32]={
 0xcc,0xfa,0x5b,0x9f,0xea,0x80,0x77,0x5a,0x07,0x50,0x7c,0x5d,0xe6,0xde,0x64,0x24,
 0x47,0x27,0xf6,0xf5,0xf3,0xd4,0x90,0xc3,0xb0,0xb8,0x12,0xb0,0x12,0x56,0xa0,0xe5};

int main(void)
{
 kr3d_vertex vertices[3]={
  {{0,0,0},{0,0,1},{0,0},0xffffffffu},
  {{1,0,0},{0,0,1},{1,0},0xff0000ffu},
  {{0,1,0},{0,0,1},{0,1},0xff00ff00u}};
 uint32_t indices[3]={0,1,2};
 kr3d_mesh_desc desc={sizeof desc,vertices,3,indices,3};
 kr3d_mesh_file_limits limits={sizeof limits,4096,16,48,4};
 kr3d_mesh_data decoded={0};kr3d_error error={0};void*blob=NULL;size_t size=0;
 assert(kr3d_mesh_file_encode(&desc,1,&blob,&size,&error));
 assert(size==KR3D_MESH_FILE_HEADER_SIZE+120u);
 assert(memcmp((uint8_t*)blob+72,expected_sha,sizeof expected_sha)==0);
 assert(kr3d_mesh_file_decode(blob,size,&limits,&decoded,&error));
 assert(decoded.vertex_count==3&&decoded.index_count==3&&decoded.material_slot_count==1);
 assert(decoded.bounds.min.x==0&&decoded.bounds.min.y==0&&decoded.bounds.max.x==1&&decoded.bounds.max.y==1);
 assert(memcmp(decoded.indices,indices,sizeof indices)==0);
 kr3d_mesh_data_release(&decoded);

 ((uint8_t*)blob)[KR3D_MESH_FILE_HEADER_SIZE]^=1u;
 assert(!kr3d_mesh_file_decode(blob,size,&limits,&decoded,&error));
 assert(error.code==KR3D_ERROR_ARGUMENT);
 ((uint8_t*)blob)[KR3D_MESH_FILE_HEADER_SIZE]^=1u;
 ((uint8_t*)blob)[8]=0; /* Header-size mismatch is never accepted as another version. */
 assert(!kr3d_mesh_file_decode(blob,size,&limits,&decoded,&error));
 ((uint8_t*)blob)[8]=(uint8_t)KR3D_MESH_FILE_HEADER_SIZE;
 limits.max_file_bytes=size-1u;
 assert(!kr3d_mesh_file_decode(blob,size,&limits,&decoded,&error));
 limits.max_file_bytes=4096;
 ((uint8_t*)blob)[KR3D_MESH_FILE_HEADER_SIZE+108u]=7; /* index 0 becomes 7 */
 /* Fixing the checksum is deliberately unavailable to an untrusted loader;
    this mutation is still rejected before any allocation escapes. */
 assert(!kr3d_mesh_file_decode(blob,size,&limits,&decoded,&error));
 kr3d_mesh_file_bytes_free(blob);

 vertices[1].normal[2]=100.0f;
 assert(!kr3d_mesh_file_encode(&desc,1,&blob,&size,&error));
 assert(blob==NULL&&size==0);
 puts("kilix-render-3d: mesh file tests passed");
 return 0;
}
