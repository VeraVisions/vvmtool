#ifndef __VVM_H__
#define __VVM_H__

#define VVM_MAGIC "INTERQUAKEMODEL"
#define VVM_VERSION 2

struct vvmheader
{
	char magic[16];
	unsigned int version;
	unsigned int filesize;
	unsigned int flags;
	unsigned int num_text, ofs_text;
	unsigned int num_meshes, ofs_meshes;
	unsigned int num_vertexarrays, num_vertexes, ofs_vertexarrays;
	unsigned int num_triangles, ofs_triangles, ofs_adjacency;
	unsigned int num_joints, ofs_joints;
	unsigned int num_poses, ofs_poses;
	unsigned int num_anims, ofs_anims;
	unsigned int num_frames, num_framechannels, ofs_frames, ofs_bounds;
	unsigned int num_comment, ofs_comment;
	unsigned int num_extensions, ofs_extensions;
};

struct vvmmesh
{
	unsigned int name;
	unsigned int material;
	unsigned int first_vertex, num_vertexes;
	unsigned int first_triangle, num_triangles;
};

enum
{
	VVM_POSITION     = 0,
	VVM_TEXCOORD     = 1,
	VVM_NORMAL       = 2,
	VVM_TANGENT      = 3,
	VVM_BLENDINDEXES = 4,
	VVM_BLENDWEIGHTS = 5,
	VVM_COLOR        = 6,
	VVM_CUSTOM       = 0x10
};

enum
{
	VVM_BYTE   = 0,
	VVM_UBYTE  = 1,
	VVM_SHORT  = 2,
	VVM_USHORT = 3,
	VVM_INT    = 4,
	VVM_UINT   = 5,
	VVM_HALF   = 6,
	VVM_FLOAT  = 7,
	VVM_DOUBLE = 8
};

struct vvmtriangle
{
	unsigned int vertex[3];
};

struct vvmadjacency
{
	unsigned int triangle[3];
};

struct vvmjointv1
{
	unsigned int name;
	int parent;
	float translate[3], rotate[3], scale[3];
};

struct vvmjoint
{
	unsigned int name;
	int parent;
	float translate[3], rotate[4], scale[3];
};

struct vvmposev1
{
	int parent;
	unsigned int mask;
	float channeloffset[9];
	float channelscale[9];
};

struct vvmpose
{
	int parent;
	unsigned int mask;
	float channeloffset[10];
	float channelscale[10];
};

struct vvmanim
{
	unsigned int name;
	unsigned int first_frame, num_frames;
	float framerate;
	unsigned int flags;
};

enum
{
	VVM_LOOP = 1<<0
};

struct vvmvertexarray
{
	unsigned int type;
	unsigned int flags;
	unsigned int format;
	unsigned int size;
	unsigned int offset;
};

struct vvmbounds
{
	float bbmin[3], bbmax[3];
	float xyradius, radius;
};

struct vvmextension
{
	unsigned int name;
	unsigned int num_data, ofs_data;
	unsigned int ofs_extensions; // pointer to next extension
};

struct vvmext_fte_mesh
{
	unsigned int contents;          //default CONTENTS_BODY
	unsigned int surfaceflags;      //propagates to trace_surfaceflags
	unsigned int body;                      //the part of the body that this mesh is meant to be from
	unsigned int geomset;
	unsigned int geomid;
	float mindist;
	float maxdist;
};
struct vvmext_fte_events
{
	unsigned int anim;
	float timestamp;
	unsigned int evcode;
	unsigned int evdata_str;
};
#endif

