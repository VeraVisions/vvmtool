#include "util.h"

#define VVM_UNPACK (1u<<31)     //animations will be unpacked into individual frames-as-animations (ie: no more framegroups)
#define VVM_ALLPRIVATE (VVM_UNPACK)

bool noext = false; /* not fully working */
bool verbose = false;
bool quiet = false;

bool ext_detected = false;

struct ejoint
{
	const char *name;
	int parent;

	ejoint() : name(NULL), parent(-1) {
	}
};

struct triangle { uint vert[3]; triangle() {
		  }
	          triangle(uint v0, uint v1, uint v2) {
			  vert[0] = v0; vert[1] = v1; vert[2] = v2;
		  }
};

vector<triangle> triangles, neighbors;

struct mesh { uint name, material; uint firstvert, numverts; uint firsttri, numtris; mesh() : name(0), material(0), firstvert(0), numverts(0), firsttri(0), numtris(0) {} };

vector<mesh> meshes;

struct meshprop
{
	uint contents;
	uint surfaceflags;
	uint body;
	uint geomset;
	uint geomid;
	float mindist;
	float maxdist;
	meshprop() : contents(0x02000000), surfaceflags(0), body(0), geomset(~0u), geomid(0), mindist(0), maxdist(0) {
	};
};
vector<meshprop> meshes_fte;
uint modelflags;

struct event_fte
{
	uint anim;
	float timestamp;
	uint evcode;
	const char *evdata_str;
	uint evdata_idx;
};
vector<event_fte> events_fte;

struct anim { uint name; uint firstframe, numframes; float fps; uint flags; anim() : name(0), firstframe(0), numframes(0), fps(0), flags(0) {
	      }
};
vector<anim> anims;

struct joint { int group; uint name; int parent; float pos[3], orient[4], scale[3]; joint() : name(0), parent(-1) {
		       memset(pos, 0, sizeof(pos)); memset(orient, 0, sizeof(orient)); memset(scale, 0, sizeof(scale));
	       }
};
vector<joint> joints;   //for meshes

struct pose { const char *name; int parent; uint flags; float offset[10], scale[10]; pose() : name(NULL), parent(-1), flags(0) {
		      memset(offset, 0, sizeof(offset)); memset(scale, 0, sizeof(scale));
	      }
};
vector<pose> poses;             //aka: animation joints

struct framebounds { Vec3 bbmin, bbmax; double xyradius, radius; framebounds() : bbmin(0, 0, 0), bbmax(0, 0, 0), xyradius(0), radius(0) {
		     }
};
vector<framebounds> bounds;

struct transform
{
	Vec3 pos;
	Quat orient;
	Vec3 scale;

	transform() {
	}
	transform(const Vec3 &pos, const Quat &orient, const Vec3 &scale = Vec3(1, 1, 1)) : pos(pos), orient(orient), scale(scale) {
	}
};
struct frame
{
	struct framepose
	{
		int remap;
		const char *bonename;
		int boneparent;
		transform tr;

		framepose() : bonename(""),boneparent(-1),tr() {
		}
		framepose(ejoint &j, transform t) : bonename(j.name),boneparent(j.parent),tr(t) {
		}
	};
	vector<framepose> pose;
};
vector<frame> frames;

vector<char> stringdata, commentdata;

uint numfverts; //verts generated so far

struct boneoverride
{
	const char *name;
	bool used;
	struct prop
	{
		const char *rename;
		int group;

		prop() : rename(NULL), group(-1) {
		}
	} props;

	boneoverride() : used(false), props(){
	}
};
vector<boneoverride> boneoverrides;
struct meshoverride
{
	const char *name;
	meshprop props;
};
vector<meshoverride> meshoverrides;


struct hitbox
{
	int body;
	const char *bone;
	Vec3 mins, maxs;
};

struct filespec
{
	const char *file;
	const char *name;
	double fps;
	uint flags;
	int startframe;
	int endframe;
	meshprop meshprops;
	const char *materialprefix;
	Quat rotate;
	float scale;
	Vec3 translate;
	bool nomesh;
	bool noanim;
	vector<event_fte> events;

	filespec() {
		reset();
	}

	void reset()
	{
		file = NULL;
		name = NULL;
		fps = 0;
		flags = 0;
		startframe = 0;
		endframe = -1;
		meshprops = meshprop();
		materialprefix = NULL;
		rotate = Quat(0, 0, 0, 1);
		scale = 1;
		translate = Vec3(0,0,0);
		nomesh = false;
		noanim = false;
		events.setsize(0);
	}
};

struct sharedstring
{
	uint offset;
	sharedstring() {
	}
	sharedstring(const char *s) : offset(stringdata.length()) {
		stringdata.put(s, strlen(s)+1);
	}
};

static inline bool htcmp(const char *x, const sharedstring &s)
{
	return htcmp(x, &stringdata[s.offset]);
}

hashtable<sharedstring, uint> stringoffsets;

uint sharestring(const char *s)
{
	if(stringdata.empty()) stringoffsets.access("", 0);
	return stringoffsets.access(s ? s : "", stringdata.length());
}

struct blendcombo
{
	int sorted;
	double weights[4];
	uchar bones[4];

	blendcombo() : sorted(0) {
	}

	void reset() {
		sorted = 0;
	}

	void addweight(double weight, int bone)
	{
		if(weight <= 1e-3) return;
		loopk(sorted) if(weight > weights[k])
		{
			for(int l = min(sorted-1, 2); l >= k; l--)
			{
				weights[l+1] = weights[l];
				bones[l+1] = bones[l];
			}
			weights[k] = weight;
			bones[k] = bone;
			if(sorted<4) sorted++;
			return;
		}
		if(sorted>=4) return;
		weights[sorted] = weight;
		bones[sorted] = bone;
		sorted++;
	}

	void finalize()
	{
		loopj(4-sorted) {
			weights[sorted+j] = 0; bones[sorted+j] = 0;
		}
		if(sorted <= 0) return;
		double total = 0;
		loopj(sorted) total += weights[j];
		total = 1.0/total;
		loopj(sorted) weights[j] *= total;
	}

	void serialize(uchar *vweights) const
	{
		int total = 0;
		loopk(4) total += (vweights[k] = uchar(0.5 + weights[k]*255));
		if(sorted <= 0) return;
		while(total > 255)
		{
			loopk(4) if(vweights[k] > 0 && total > 255) { vweights[k]--; total--; }
		}
		while(total < 255)
		{
			loopk(4) if(vweights[k] < 255 && total < 255) { vweights[k]++; total++; }
		}
	}

	bool operator==(const blendcombo &c) {
		loopi(4) if(weights[i] != c.weights[i] || bones[i] != c.bones[i]) return false; return true;
	}
	bool operator!=(const blendcombo &c) {
		loopi(4) if(weights[i] != c.weights[i] || bones[i] != c.bones[i]) return true; return false;
	}
};
vector<Vec4> mpositions;
vector<blendcombo> mblends;

static bool parseindex(char *&c, int &val)
{
	while(isspace(*c)) c++;
	char *end = NULL;
	int rval = strtol(c, &end, 10);
	if(c == end) return false;
	val = rval;
	c = end;
	return true;
}

static double parseattrib(char *&c, double ival = 0)
{
	while(isspace(*c)) c++;
	char *end = NULL;
	double val = strtod(c, &end);
	if(c == end) val = ival;
	else c = end;
	return val;
}

static bool maybeparseattrib(char *&c, double &result)
{
	while(isspace(*c)) c++;
	char *end = NULL;
	double val = strtod(c, &end);
	if(c == end) return false;
	c = end;
	result = val;
	return true;
}

#if 0
static bool parsename(char *&c, char *buf, int bufsize = sizeof(string))
{
	while(isspace(*c)) c++;
	char *end;
	if(*c == '"')
	{
		c++;
		end = c;
		while(*end && *end != '"') end++;
		copystring(buf, c, min(int(end-c+1), bufsize));
		if(*end == '"') end++;
	}
	else
	{
		end = c;
		while(*end && !isspace(*end)) end++;
		copystring(buf, c, min(int(end-c+1), bufsize));
	}
	if(c == end) return false;
	c = end;
	return true;
}
#endif

static char *trimname(char *&c)
{
	while(isspace(*c)) c++;
	char *start, *end;
	if(*c == '"')
	{
		c++;
		start = end = c;
		while(*end && *end != '"') end++;
		if(*end) { *end = '\0'; end++; }
	}
	else
	{
		start = end = c;
		while(*end && !isspace(*end)) end++;
		if(*end) { *end = '\0'; end++; }
	}
	c = end;
	return start;
}

static Vec4 parseattribs4(char *&c, const Vec4 &ival = Vec4(0, 0, 0, 0))
{
	Vec4 val;
	loopk(4) val[k] = parseattrib(c, ival[k]);
	return val;
}

static Vec3 parseattribs3(char *&c, const Vec3 &ival = Vec3(0, 0, 0))
{
	Vec3 val;
	loopk(3) val[k] = parseattrib(c, ival[k]);
	return val;
}

static blendcombo parseblends(char *&c)
{
	blendcombo b;
	int index;
	while(parseindex(c, index))
	{
		double weight = parseattrib(c, 0);
		b.addweight(weight, index);
	}
	b.finalize();
	return b;
}

struct eanim
{
	const char *name;
	int startframe, endframe;
	double fps;
	uint flags;

	eanim() : name(NULL), startframe(0), endframe(INT_MAX), fps(0), flags(0) {
	}
};

struct emesh
{
	const char *name, *material;
	int firsttri;
	bool used;
	bool hasexplicits;
	meshprop explicits;

	emesh() : name(NULL), material(NULL), firsttri(0), used(false), hasexplicits(false) {
	}
	emesh(const char *name, const char *material, int firsttri = 0) : name(name), material(material), firsttri(firsttri), used(false) {
	}
};

struct evarray
{
	string name;
	uint type, format, size;

	evarray() : type(VVM_POSITION), format(VVM_FLOAT), size(3) {
		name[0] = '\0';
	}
	evarray(uint type, uint format, uint size, const char *initname = "") : type(type), format(format), size(size) {
		copystring(name, initname);
	}
};

struct esmoothgroup
{
	enum
	{
		F_USED     = 1<<0,
		F_UVSMOOTH = 1<<1
	};

	int key;
	float angle;
	int flags;

	esmoothgroup() : key(-1), angle(-1), flags(0) {
	}
};

struct etriangle
{
	int smoothgroup;
	uint vert[3], weld[3];

	etriangle()
		: smoothgroup(-1)
	{
	}
	etriangle(int v0, int v1, int v2, int smoothgroup = -1)
		: smoothgroup(smoothgroup)
	{
		vert[0] = v0;
		vert[1] = v1;
		vert[2] = v2;
	}
};

vector<Vec4> epositions, etexcoords, etangents, ecolors, ecustom[10];
vector<Vec3> enormals, ebitangents;
vector<blendcombo> eblends;
vector<etriangle> etriangles;
vector<esmoothgroup> esmoothgroups;
vector<int> esmoothindexes;
vector<uchar> esmoothedges;
vector<ejoint> ejoints;
vector<transform> eposes;
vector<Matrix3x4> mjoints;
vector<int> eframes;
vector<eanim> eanims;
vector<emesh> emeshes;
vector<evarray> evarrays;
hashtable<const char *, char *> enames;

const char *getnamekey(const char *name)
{
	char **exists = enames.access(name);
	if(exists) return *exists;
	char *key = newstring(name);
	enames[key] = key;
	return key;
}

struct weldinfo
{
	int tri, vert;
	weldinfo *next;
};

void weldvert(const vector<Vec3> &norms, const Vec4 &pos, weldinfo *welds, int &numwelds, unionfind<int> &welder)
{
	welder.clear();
	int windex = 0;
	for(weldinfo *w = welds; w; w = w->next, windex++)
	{
		etriangle &wt = etriangles[w->tri];
		esmoothgroup &wg = esmoothgroups[wt.smoothgroup];
		int vindex = windex + 1;
		for(weldinfo *v = w->next; v; v = v->next, vindex++)
		{
			etriangle &vt = etriangles[v->tri];
			esmoothgroup &vg = esmoothgroups[vt.smoothgroup];
			if(wg.key != vg.key) continue;
			if(norms[w->tri].dot(norms[v->tri]) < max(wg.angle, vg.angle)) continue;
			if(((wg.flags | vg.flags) & esmoothgroup::F_UVSMOOTH) &&
			   etexcoords[wt.vert[w->vert]] != etexcoords[vt.vert[v->vert]])
				continue;
			if(esmoothindexes.length() > max(w->vert, v->vert) && esmoothindexes[w->vert] != esmoothindexes[v->vert])
				continue;
			if(esmoothedges.length())
			{
				int w0 = w->vert, w1 = (w->vert+1)%3, w2 = (w->vert+2)%3;
				const Vec4 &wp1 = epositions[wt.vert[w1]],
				&wp2 = epositions[wt.vert[w2]];
				int v0 = v->vert, v1 = (v->vert+1)%3, v2 = (v->vert+2)%3;
				const Vec4 &vp1 = epositions[vt.vert[v1]],
				&vp2 = epositions[vt.vert[v2]];
				int wf = esmoothedges[w->tri], vf = esmoothedges[v->tri];
				if((wp1 != vp1 || !(((wf>>w0)|(vf>>v0))&1)) &&
				   (wp1 != vp2 || !(((wf>>w0)|(vf>>v2))&1)) &&
				   (wp2 != vp1 || !(((wf>>w2)|(vf>>v0))&1)) &&
				   (wp2 != vp2 || !(((wf>>w2)|(vf>>v2))&1)))
					continue;
			}
			welder.unite(windex, vindex, -1);
		}
	}
	windex = 0;
	for(weldinfo *w = welds; w; w = w->next, windex++)
	{
		etriangle &wt = etriangles[w->tri];
		wt.weld[w->vert] = welder.find(windex, -1, numwelds);
		if(wt.weld[w->vert] == uint(numwelds)) numwelds++;
	}
}

void smoothverts(bool areaweight = true)
{
	if(etriangles.empty()) return;

	if(enormals.length())
	{
		loopv(etriangles)
		{
			etriangle &t = etriangles[i];
			loopk(3) t.weld[k] = t.vert[k];
		}
		return;
	}

	if(etexcoords.empty()) loopv(esmoothgroups) esmoothgroups[i].flags &= ~esmoothgroup::F_UVSMOOTH;
	if(esmoothedges.length()) while(esmoothedges.length() < etriangles.length()) esmoothedges.add(7);

	vector<Vec3> tarea, tnorms;
	loopv(etriangles)
	{
		etriangle &t = etriangles[i];
		Vec3 v0(epositions[t.vert[0]]),
		v1(epositions[t.vert[1]]),
		v2(epositions[t.vert[2]]);
		tnorms.add(tarea.add((v2 - v0).cross(v1 - v0)).normalize());
	}

	int nextalloc = 0;
	vector<weldinfo *> allocs;
	hashtable<Vec4, weldinfo *> welds(1<<12);

	loopv(etriangles)
	{
		etriangle &t = etriangles[i];
		loopk(3)
		{
			weldinfo **next = &welds.access(epositions[t.vert[k]], NULL);
			if(!(nextalloc % 1024)) allocs.add(new weldinfo[1024]);
			weldinfo &w = allocs[nextalloc/1024][nextalloc%1024];
			nextalloc++;
			w.tri = i;
			w.vert = k;
			w.next = *next;
			*next = &w;
		}
	}

	int numwelds = 0;
	unionfind<int> welder;
	enumerate(welds, Vec4, vpos, weldinfo *, vwelds, weldvert(tnorms, vpos, vwelds, numwelds, welder));

	loopv(allocs) delete[] allocs[i];

	loopi(numwelds) enormals.add(Vec3(0, 0, 0));
	loopv(etriangles)
	{
		etriangle &t = etriangles[i];
		loopk(3) enormals[t.weld[k]]+= areaweight ? tarea[i] : tnorms[i];
	}
	loopv(enormals) if(enormals[i] != Vec3(0, 0, 0)) enormals[i] = enormals[i].normalize();
}

struct sharedvert
{
	int index, weld;

	sharedvert() {
	}
	sharedvert(int index, int weld) : index(index), weld(weld) {
	}
};

static inline bool htcmp(const sharedvert &v, const sharedvert &s)
{
	if(epositions[v.index] != epositions[s.index]) return false;
	if(etexcoords.length() && etexcoords[v.index] != etexcoords[s.index]) return false;
	if(enormals.length() && enormals[v.weld] != enormals[s.weld]) return false;
	if(eblends.length() && eblends[v.index] != eblends[s.index]) return false;
	if(ecolors.length() && ecolors[v.index] != ecolors[s.index]) return false;
	loopi(10) if(ecustom[i].length() && ecustom[i][v.index] != ecustom[i][s.index]) return false;
	return true;
}

static inline uint hthash(const sharedvert &v)
{
	return hthash(epositions[v.index]);
}

const struct vertexarraytype
{
	const char *name;
	int code;
} vatypes[] =
{
	{ "position", VVM_POSITION },
	{ "texcoord", VVM_TEXCOORD },
	{ "normal", VVM_NORMAL  },
	{ "tangent", VVM_TANGENT },
	{ "blendindexes", VVM_BLENDINDEXES },
	{ "blendweights", VVM_BLENDWEIGHTS },
	{ "color", VVM_COLOR },
	{ "custom0", VVM_CUSTOM + 0 },
	{ "custom1", VVM_CUSTOM + 1 },
	{ "custom2", VVM_CUSTOM + 2 },
	{ "custom3", VVM_CUSTOM + 3 },
	{ "custom4", VVM_CUSTOM + 4 },
	{ "custom5", VVM_CUSTOM + 5 },
	{ "custom6", VVM_CUSTOM + 6 },
	{ "custom7", VVM_CUSTOM + 7 },
	{ "custom8", VVM_CUSTOM + 8 },
	{ "custom9", VVM_CUSTOM + 9 }
};

int findvertexarraytype(const char *name)
{
	loopi(sizeof(vatypes)/sizeof(vatypes[0]))
	{
		if(!strcasecmp(vatypes[i].name, name))
			return vatypes[i].code;
	}
	return -1;
}

const struct vertexarrayformat
{
	const char *name;
	int code;
	int size;
} vaformats[] =
{
	{ "byte", VVM_BYTE, 1 },
	{ "ubyte", VVM_UBYTE, 1 },
	{ "short", VVM_SHORT, 2 },
	{ "ushort", VVM_USHORT, 2 },
	{ "int", VVM_INT, 4 },
	{ "uint", VVM_UINT, 4 },
	{ "half", VVM_HALF, 2 },
	{ "float", VVM_FLOAT, 4 },
	{ "double", VVM_DOUBLE, 8 }
};

int findvertexarrayformat(const char *name)
{
	loopi(sizeof(vaformats)/sizeof(vaformats[0]))
	{
		if(!strcasecmp(vaformats[i].name, name))
			return vaformats[i].code;
	}
	return -1;
}

struct vertexarray
{
	uint type, flags, format, size, offset, count;
	vector<uchar> vdata;

	vertexarray(uint type, uint format, uint size) : type(type), flags(0), format(format), size(size), offset(0), count(0) {
	}

	int formatsize() const
	{
		return vaformats[format].size;
	}

	int bytesize() const
	{
		return size * vaformats[format].size;
	}
};

vector<sharedvert> vmap;
vector<vertexarray> varrays;
vector<uchar> vdata;

struct halfdata
{
	ushort val;

	halfdata(double d)
	{
		union
		{
			ullong i;
			double d;
		} conv;
		conv.d = d;
		ushort signbit = ushort((conv.i>>63)&1);
		ushort mantissa = ushort((conv.i>>(52-10))&0x3FF);
		int exponent = int((conv.i>>52)&0x7FF) - 1023 + 15;
		if(exponent <= 0)
		{
			mantissa |= 0x400;
			mantissa >>= min(1-exponent, 10+1);
			exponent = 0;
		}
		else if(exponent >= 0x1F)
		{
			mantissa = 0;
			exponent = 0x1F;
		}
		val = (signbit<<15) | (ushort(exponent)<<10) | mantissa;
	}
};

template<> inline halfdata endianswap<halfdata>(halfdata n) {
	n.val = endianswap16(n.val); return n;
}

template<int TYPE> static inline int remapindex(int i, const sharedvert &v) {
	return v.index;
}
template<> inline int remapindex<VVM_NORMAL>(int i, const sharedvert &v) {
	return v.weld;
}
template<> inline int remapindex<VVM_TANGENT>(int i, const sharedvert &v) {
	return i;
}

template<class T, class U>
static inline void putattrib(T &out, const U &val) {
	out = T(val);
}

template<class T, class U>
static inline void uroundattrib(T &out, const U &val, double scale) {
	out = T(clamp(0.5 + val*scale, 0.0, scale));
}
template<class T, class U>
static inline void sroundattrib(T &out, const U &val, double scale, double low, double high) {
	double n = val*scale*0.5; out = T(clamp(n < 0 ? ceil(n - 1) : floor(n), low, high));
}

template<class T, class U>
static inline void scaleattrib(T &out, const U &val) {
	putattrib(out, val);
}
template<class U>
static inline void scaleattrib(char &out, const U &val) {
	sroundattrib(out, val, 255.0, -128.0, 127.0);
}
template<class U>
static inline void scaleattrib(short &out, const U &val) {
	sroundattrib(out, val, 65535.0, -32768.0, 32767.0);
}
template<class U>
static inline void scaleattrib(int &out, const U &val) {
	sroundattrib(out, val, 4294967295.0, -2147483648.0, 2147483647.0);
}
template<class U>
static inline void scaleattrib(uchar &out, const U &val) {
	uroundattrib(out, val, 255.0);
}
template<class U>
static inline void scaleattrib(ushort &out, const U &val) {
	uroundattrib(out, val, 65535.0);
}
template<class U>
static inline void scaleattrib(uint &out, const U &val) {
	uroundattrib(out, val, 4294967295.0);
}

template<int T>
static inline bool normalizedattrib() {
	return true;
}

template<int TYPE, int FMT, class T, class U>
static inline void serializeattrib(const vertexarray &va, T *data, const U &attrib)
{
	if(normalizedattrib<TYPE>()) switch(va.size)
		{
		case 4: scaleattrib(data[3], attrib.w);
		case 3: scaleattrib(data[2], attrib.z);
		case 2: scaleattrib(data[1], attrib.y);
		case 1: scaleattrib(data[0], attrib.x);
		}
	else switch(va.size)
		{
		case 4: putattrib(data[3], attrib.w);
		case 3: putattrib(data[2], attrib.z);
		case 2: putattrib(data[1], attrib.y);
		case 1: putattrib(data[0], attrib.x);
		}
	lilswap(data, va.size);
}

template<int TYPE, int FMT, class T>
static inline void serializeattrib(const vertexarray &va, T *data, const Vec3 &attrib)
{
	if(normalizedattrib<TYPE>()) switch(va.size)
		{
		case 3: scaleattrib(data[2], attrib.z);
		case 2: scaleattrib(data[1], attrib.y);
		case 1: scaleattrib(data[0], attrib.x);
		}
	else switch(va.size)
		{
		case 3: putattrib(data[2], attrib.z);
		case 2: putattrib(data[1], attrib.y);
		case 1: putattrib(data[0], attrib.x);
		}
	lilswap(data, va.size);
}

template<int TYPE, int FMT, class T>
static inline void serializeattrib(const vertexarray &va, T *data, const blendcombo &blend)
{
	if(TYPE == VVM_BLENDINDEXES)
	{
		switch(va.size)
		{
		case 4: putattrib(data[3], blend.bones[3]);
		case 3: putattrib(data[2], blend.bones[2]);
		case 2: putattrib(data[1], blend.bones[1]);
		case 1: putattrib(data[0], blend.bones[0]);
		}
	}
	else if(FMT == VVM_UBYTE)
	{
		uchar weights[4];
		blend.serialize(weights);
		switch(va.size)
		{
		case 4: putattrib(data[3], weights[3]);
		case 3: putattrib(data[2], weights[2]);
		case 2: putattrib(data[1], weights[1]);
		case 1: putattrib(data[0], weights[0]);
		}
	}
	else
	{
		switch(va.size)
		{
		case 4: scaleattrib(data[3], blend.weights[3]);
		case 3: scaleattrib(data[2], blend.weights[2]);
		case 2: scaleattrib(data[1], blend.weights[1]);
		case 1: scaleattrib(data[0], blend.weights[0]);
		}
	}
	lilswap(data, va.size);
}

template<int TYPE, class T>
void setupvertexarray(const vector<T> &attribs, uint type, uint fmt, uint size, uint first)
{
	const char *name = "";
	loopv(evarrays) if(evarrays[i].type == type)
	{
		evarray &info = evarrays[i];
		fmt = info.format;
		size = (uint)clamp((int)info.size, 1, 4);
		name = info.name;
		break;
	}

	if(type >= VVM_CUSTOM)
	{
		if(!name[0])
		{
			defformatstring(customname, "custom%d", type-VVM_CUSTOM);
			type = VVM_CUSTOM + sharestring(customname);
		}
		else type = VVM_CUSTOM + sharestring(name);
	}

	int k;
	for (k = 0; k < varrays.length(); k++)
	{
		if (varrays[k].type == type && varrays[k].format == fmt && varrays[k].size == size)
			break;
	}
	if (k == varrays.length())
		varrays.add(vertexarray(type, fmt, size));
	vertexarray &va = varrays[k];
	if (va.count != first)
		fatal("count != first");        //gaps are a problem.
	va.count += vmap.length();

	int totalsize = va.bytesize() * vmap.length();
	uchar *data = va.vdata.reserve(totalsize);
	va.vdata.advance(totalsize);
	loopv(vmap)
	{
		const T &attrib = attribs[remapindex<TYPE>(i, vmap[i])];
		switch(va.format)
		{
		case VVM_BYTE: serializeattrib<TYPE, VVM_BYTE>(va, (char *)data, attrib); break;
		case VVM_UBYTE: serializeattrib<TYPE, VVM_UBYTE>(va, (uchar *)data, attrib); break;
		case VVM_SHORT: serializeattrib<TYPE, VVM_SHORT>(va, (short *)data, attrib); break;
		case VVM_USHORT: serializeattrib<TYPE, VVM_USHORT>(va, (ushort *)data, attrib); break;
		case VVM_INT: serializeattrib<TYPE, VVM_INT>(va, (int *)data, attrib); break;
		case VVM_UINT: serializeattrib<TYPE, VVM_UINT>(va, (uint *)data, attrib); break;
		case VVM_HALF: serializeattrib<TYPE, VVM_HALF>(va, (halfdata *)data, attrib); break;
		case VVM_FLOAT: serializeattrib<TYPE, VVM_FLOAT>(va, (float *)data, attrib); break;
		case VVM_DOUBLE: serializeattrib<TYPE, VVM_DOUBLE>(va, (double *)data, attrib); break;
		}
		data += va.bytesize();
	}
}

// linear speed vertex cache optimization from Tom Forsyth

#define MAXVCACHE 32

struct triangleinfo
{
	bool used;
	float score;
	uint vert[3];

	triangleinfo() {
	}
	triangleinfo(uint v0, uint v1, uint v2)
	{
		vert[0] = v0;
		vert[1] = v1;
		vert[2] = v2;
	}
};

struct vertexcache : listnode<vertexcache>
{
	int index, rank;
	float score;
	int numuses;
	triangleinfo **uses;

	vertexcache() : index(-1), rank(-1), score(-1.0f), numuses(0), uses(NULL) {
	}

	void calcscore()
	{
		if(numuses > 0)
		{
			score = 2.0f * powf(numuses, -0.5f);
			if(rank >= 3) score += powf(1.0f - (rank - 3)/float(MAXVCACHE - 3), 1.5f);
			else if(rank >= 0) score += 0.75f;
		}
		else score = -1.0f;
	}

	void removeuse(triangleinfo *t)
	{
		loopi(numuses) if(uses[i] == t)
		{
			uses[i] = uses[--numuses];
			return;
		}
	}
};

void maketriangles(vector<triangleinfo> &tris, const vector<sharedvert> &mmap)
{
	triangleinfo **uses = new triangleinfo *[3*tris.length()];
	vertexcache *verts = new vertexcache[mmap.length()];
	list<vertexcache> vcache;

	loopv(tris)
	{
		triangleinfo &t = tris[i];
		t.used = t.vert[0] == t.vert[1] || t.vert[1] == t.vert[2] || t.vert[2] == t.vert[0];
		if(t.used) continue;
		loopk(3) verts[t.vert[k]].numuses++;
	}
	triangleinfo **curuse = uses;
	loopvrev(tris)
	{
		triangleinfo &t = tris[i];
		if(t.used) continue;
		loopk(3)
		{
			vertexcache &v = verts[t.vert[k]];
			if(!v.uses) { curuse += v.numuses; v.uses = curuse; }
			*--v.uses = &t;
		}
	}
	loopv(mmap) verts[i].calcscore();
	triangleinfo *besttri = NULL;
	float bestscore = -1e16f;
	loopv(tris)
	{
		triangleinfo &t = tris[i];
		if(t.used) continue;
		t.score = verts[t.vert[0]].score + verts[t.vert[1]].score + verts[t.vert[2]].score;
		if(t.score > bestscore) { besttri = &t; bestscore = t.score; }
	}

	//int reloads = 0;
	while(besttri)
	{
		besttri->used = true;
		triangle &t = triangles.add();
		loopk(3)
		{
			vertexcache &v = verts[besttri->vert[k]];
			if(v.index < 0) { v.index = vmap.length(); vmap.add(mmap[besttri->vert[k]]); }
			t.vert[k] = v.index;
			v.removeuse(besttri);
			if(v.rank >= 0) vcache.remove(&v)->rank = -1;
//			else reloads++;
			if(v.numuses <= 0) continue;
			vcache.insertfirst(&v);
			v.rank = 0;
		}
		int rank = 0;
		for(vertexcache *v = vcache.first(); v != vcache.end(); v = v->next)
		{
			v->rank = rank++;
			v->calcscore();
		}
		besttri = NULL;
		bestscore = -1e16f;
		for(vertexcache *v = vcache.first(); v != vcache.end(); v = v->next)
		{
			loopi(v->numuses)
			{
				triangleinfo &t = *v->uses[i];
				t.score = verts[t.vert[0]].score + verts[t.vert[1]].score + verts[t.vert[2]].score;
				if(t.score > bestscore) { besttri = &t; bestscore = t.score; }
			}
		}
		while(vcache.size > MAXVCACHE) vcache.removelast()->rank = -1;
		if(!besttri) loopv(tris)
			{
				triangleinfo &t = tris[i];
				if(!t.used && t.score > bestscore) { besttri = &t; bestscore = t.score; }
			}
	}
//	printf("reloads: %d, worst: %d, best: %d\n", reloads, tris.length()*3, mmap.length());

	delete[] uses;
	delete[] verts;
}

void calctangents(uint priortris, bool areaweight = true)
{
	uint numverts = vmap.length();
	Vec3 *tangent = new Vec3[2*numverts], *bitangent = tangent+numverts;
	memset(tangent, 0, 2*numverts*sizeof(Vec3));
	for (int i = priortris; i < triangles.length(); i++)
	{
		const triangle &t = triangles[i];
		sharedvert &i0 = vmap[t.vert[0]],
		&i1 = vmap[t.vert[1]],
		&i2 = vmap[t.vert[2]];

		Vec3 v0(epositions[i0.index]), e1 = Vec3(epositions[i1.index]) - v0, e2 = Vec3(epositions[i2.index]) - v0;

		double u1 = etexcoords[i1.index].x - etexcoords[i0.index].x, v1 = etexcoords[i1.index].y - etexcoords[i0.index].y,
		       u2 = etexcoords[i2.index].x - etexcoords[i0.index].x, v2 = etexcoords[i2.index].y - etexcoords[i0.index].y;
		Vec3 u = e2*v1 - e1*v2,
		     v = e2*u1 - e1*u2;

		if(e2.cross(e1).dot(v.cross(u)) < 0)
		{
			u = -u;
			v = -v;
		}

		if(!areaweight)
		{
			u = u.normalize();
			v = v.normalize();
		}

		loopj(3)
		{
			tangent[t.vert[j]] += u;
			bitangent[t.vert[j]] += v;
		}
	}
	loopv(vmap)
	{
		const Vec3 &n = enormals[vmap[i].weld],
		&t = tangent[i],
		&bt = bitangent[i];
		etangents.add(Vec4((t - n*n.dot(t)).normalize(), n.cross(t).dot(bt) < 0 ? -1 : 1));
	}
	delete[] tangent;
}

struct neighborkey
{
	uint e0, e1;

	neighborkey() {
	}
	neighborkey(uint i0, uint i1)
	{
		if(epositions[i0] < epositions[i1]) { e0 = i0; e1 = i1; }
		else { e0 = i1; e1 = i0; }
	}

	uint hash() const {
		return hthash(epositions[e0]) + hthash(epositions[e1]);
	}
	bool operator==(const neighborkey &n) const
	{
		return epositions[e0] == epositions[n.e0] && epositions[e1] == epositions[n.e1] &&
		       (eblends.empty() || (eblends[e0] == eblends[n.e0] && eblends[e1] == eblends[n.e1]));
	}
};

static inline uint hthash(const neighborkey &n) {
	return n.hash();
}
static inline bool htcmp(const neighborkey &x, const neighborkey &y) {
	return x == y;
}

struct neighborval
{
	uint tris[2];

	neighborval() {
	}
	neighborval(uint i) {
		tris[0] = i; tris[1] = 0xFFFFFFFFU;
	}

	void add(uint i)
	{
		if(tris[1] != 0xFFFFFFFFU) tris[0] = tris[1] = 0xFFFFFFFFU;
		else if(tris[0] != 0xFFFFFFFFU) tris[1] = i;
	}

	int opposite(uint i) const
	{
		return tris[0] == i ? tris[1] : tris[0];
	}
};

void makeneighbors(uint priortris)
{
	hashtable<neighborkey, neighborval> nhash;

	for(int i = priortris; i<triangles.length(); i++)
	{
		triangle &t = triangles[i];
		for(int j = 0, p = 2; j < 3; p = j, j++)
		{
			neighborkey key(t.vert[p], t.vert[j]);
			neighborval *val = nhash.access(key);
			if(val) val->add(i);
			else nhash[key] = neighborval(i);
		}
	}

	for(int i = priortris; i<triangles.length(); i++)
	{
		triangle &t = triangles[i];
		triangle &n = neighbors.add();
		for(int j = 0, p = 2; j < 3; p = j, j++)
			n.vert[p] = nhash[neighborkey(t.vert[p], t.vert[j])].opposite(i);
	}
}

Quat erotate;
double escale = 1;
Vec3 emeshtrans(0, 0, 0);
Vec3 ejointtrans(0, 0, 0);
double gscale = 1;
Vec3 gmeshtrans(0,0,0);

void printlastmesh(void)
{
	if (quiet)
		return;
	mesh &m = meshes[meshes.length()-1];
	meshprop &fm = meshes_fte[meshes.length()-1];
	printf("    %smesh %i:\tname=\"%s\",\tmat=\"%s\",\ttri=%i, vert=%i\n", fm.contents?"c":"r", meshes.length()-1,
	       &stringdata[m.name], &stringdata[m.material], m.numtris, m.numverts);

	if (verbose)
	{
		if (noext)
			printf("        writing mesh properties is disabled\n");
		else
			printf("        c=%#x sf=%#x b=%i gs=%i gi=%i nd=%g fd=%g\n", fm.contents, fm.surfaceflags, fm.body, fm.geomset, fm.geomid, fm.maxdist, fm.mindist);
	}
}

void makemeshes(const filespec &spec)
{
	if (spec.nomesh)
		return;
	/*
	   if (meshes.length())
	        return;
	   meshes.setsize(0);
	   meshes_fte.setsize(0);
	   triangles.setsize(0);
	   neighbors.setsize(0);
	   varrays.setsize(0);
	   vdata.setsize(0);
	 */
	int priorverts = numfverts;
	int priortris = triangles.length();

	hashtable<sharedvert, uint> mshare(1<<12);
	vector<sharedvert> mmap;
	vector<triangleinfo> tinfo;

	if (!noext)
	{
		loopv(emeshes)
		{
			if (!emeshes[i].hasexplicits)
				emeshes[i].explicits = spec.meshprops;

			loopk(meshoverrides.length())
			{
				if (!strcmp(meshoverrides[k].name, emeshes[i].name))
				{
					emeshes[i].explicits = meshoverrides[k].props;
					for (; k < meshoverrides.length()-1; k++)
						meshoverrides[k] = meshoverrides[k+1];
					meshoverrides.drop();
					break;
				}
			}
		}
	}

	loopv(emeshes)
	{
		emesh &em1 = emeshes[i];
		if(em1.used) continue;
		for(int j = i; j < emeshes.length(); j++)
		{
			emesh &em = emeshes[j];
			if(strcmp(em.name, em1.name) || strcmp(em.material, em1.material) || memcmp(&em.explicits, &em1.explicits, sizeof(em.explicits))) continue;
			int lasttri = emeshes.inrange(j+1) ? emeshes[j+1].firsttri : etriangles.length();
			for(int k = em.firsttri; k < lasttri; k++)
			{
				etriangle &et = etriangles[k];
				triangleinfo &t = tinfo.add();
				loopl(3)
				{
					sharedvert v(et.vert[l], et.weld[l]);
					t.vert[l] = mshare.access(v, mmap.length());
					if(!mmap.inrange(t.vert[l])) mmap.add(v);
				}
			}
			em.used = true;
		}
		if(tinfo.empty()) continue;

		mesh &m = meshes.add();
		m.name = sharestring(em1.name);
		if (spec.materialprefix)
		{
			char material[512];
			formatstring(material, "%s%s", spec.materialprefix, em1.material);
			m.material = sharestring(material);
		}
		else
			m.material = sharestring(em1.material);
		m.firsttri = triangles.length();
		m.firstvert = numfverts+vmap.length();
		maketriangles(tinfo, mmap);
		m.numtris = triangles.length() - m.firsttri;
		m.numverts = numfverts+vmap.length() - m.firstvert;

		meshprop &mf = meshes_fte.add();
		mf = em1.explicits;

		printlastmesh();

		mshare.clear();
		mmap.setsize(0);
		tinfo.setsize(0);
	}
	numfverts+=vmap.length();

	if(triangles.length()) makeneighbors(priortris);

	if(escale != 1) loopv(epositions) epositions[i] *= escale;
	if(erotate != Quat(0, 0, 0, 1))
	{
		loopv(epositions) epositions[i].setxyz(erotate.transform(Vec3(epositions[i])));
		loopv(enormals) enormals[i] = erotate.transform(enormals[i]);
		loopv(etangents) etangents[i].setxyz(erotate.transform(Vec3(etangents[i])));
		loopv(ebitangents) ebitangents[i] = erotate.transform(ebitangents[i]);
	}
	if(emeshtrans != Vec3(0, 0, 0)) loopv(epositions) epositions[i] += emeshtrans;
	if(epositions.length()) setupvertexarray<VVM_POSITION>(epositions, VVM_POSITION, VVM_FLOAT, 3, priorverts);
	if(etexcoords.length()) setupvertexarray<VVM_TEXCOORD>(etexcoords, VVM_TEXCOORD, VVM_FLOAT, 2, priorverts);
	if(enormals.length()) setupvertexarray<VVM_NORMAL>(enormals, VVM_NORMAL, VVM_FLOAT, 3, priorverts);
	if(etangents.length())
	{
		if(ebitangents.length() && enormals.length())
		{
			loopv(etangents) if(ebitangents.inrange(i) && enormals.inrange(i))
				etangents[i].w = enormals[i].cross(Vec3(etangents[i])).dot(ebitangents[i]) < 0 ? -1 : 1;
		}
		setupvertexarray<VVM_TANGENT>(etangents, VVM_TANGENT, VVM_FLOAT, 4, priorverts);
	}
	else if(enormals.length() && etexcoords.length())
	{
		calctangents(priortris);
		setupvertexarray<VVM_TANGENT>(etangents, VVM_TANGENT, VVM_FLOAT, 4, priorverts);
	}
	if(eblends.length())
	{
		setupvertexarray<VVM_BLENDINDEXES>(eblends, VVM_BLENDINDEXES, VVM_UBYTE, 4, priorverts);
		setupvertexarray<VVM_BLENDWEIGHTS>(eblends, VVM_BLENDWEIGHTS, VVM_UBYTE, 4, priorverts);
	}
	if(ecolors.length()) setupvertexarray<VVM_COLOR>(ecolors, VVM_COLOR, VVM_UBYTE, 4, priorverts);
	loopi(10) if(ecustom[i].length()) setupvertexarray<VVM_CUSTOM>(ecustom[i], VVM_CUSTOM + i, VVM_FLOAT, 4, priorverts);

	//make sure we keep this data in a usable form so that we can calc framebounds.
	if(epositions.length())
	{
		Vec4 *o = mpositions.reserve(epositions.length());
		mpositions.advance(epositions.length());
		loopv(epositions)
		o[i] = epositions[i];
	}
	if(eblends.length())
	{
		blendcombo *o = mblends.reserve(eblends.length());
		mblends.advance(eblends.length());
		loopv(eblends)
		o[i] = eblends[i];
	}

	//the generated triangles currently refer to the imported arrays.
	//make sure they refer to the final verts
	if (priorverts)
		for (int i = priortris; i < triangles.length(); i++)
		{
			triangles[i].vert[0] += priorverts;
			triangles[i].vert[1] += priorverts;
			triangles[i].vert[2] += priorverts;
		}
}

void makebounds(framebounds &bb, Matrix3x4 *invbase, frame &frame)
{
	vector<Matrix3x4> buf;
	buf.growbuf(joints.length());
	buf.setsize(joints.length());

	//make sure all final bones have some value, even if its gibberish. should probably ignore verts that depend upon bones not defined in this animation.
	//remap<0 means the bone was dropped.
	loopv(buf) {
		buf[i] = Matrix3x4(Quat(0,0,0,1),Vec3(0,0,0));
	}
	loopv(frame.pose) if (frame.pose[i].remap>=0)
	{
		int bone = frame.pose[i].remap;
		int jparent = frame.pose[i].boneparent;
		if (jparent >= 0) jparent = frame.pose[jparent].remap;

		if(jparent >= 0) buf[bone] = buf[jparent] * Matrix3x4(frame.pose[i].tr.orient, frame.pose[i].tr.pos, frame.pose[i].tr.scale);
		else buf[bone] = Matrix3x4(frame.pose[i].tr.orient, frame.pose[i].tr.pos, frame.pose[i].tr.scale);
	}

	loopv(frame.pose) buf[i] *= invbase[i];
	loopv(mpositions)
	{
		const blendcombo &c = mblends[i];
		Matrix3x4 m(Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0));
		loopk(4) if(c.weights[k] > 0)
			m += buf[c.bones[k]] * c.weights[k];
		Vec3 p = m.transform(Vec3(mpositions[i]));

		if(!i) bb.bbmin = bb.bbmax = p;
		else
		{
			bb.bbmin.x = min(bb.bbmin.x, p.x);
			bb.bbmin.y = min(bb.bbmin.y, p.y);
			bb.bbmin.z = min(bb.bbmin.z, p.z);
			bb.bbmax.x = max(bb.bbmax.x, p.x);
			bb.bbmax.y = max(bb.bbmax.y, p.y);
			bb.bbmax.z = max(bb.bbmax.z, p.z);
		}
		double xyradius = p.x*p.x + p.y*p.y;
		bb.xyradius = max(bb.xyradius, xyradius);
		bb.radius = max(bb.radius, xyradius + p.z*p.z);
	}
	if(bb.xyradius > 0) bb.xyradius = sqrt(bb.xyradius);
	if(bb.radius > 0) bb.radius = sqrt(bb.radius);
}

void makerelativebasepose()
{
	int numbasejoints = min(ejoints.length(), eframes.length() ? eframes[0] : eposes.length());
	for(int i = numbasejoints-1; i >= 0; i--)
	{
		ejoint &ej = ejoints[i];
		if(ej.parent < 0) continue;
		transform &parent = eposes[ej.parent], &child = eposes[i];
		child.pos = (-parent.orient).transform(child.pos - parent.pos);
		child.orient = (-parent.orient)*child.orient;
		if(child.orient.w > 0) child.orient.flip();
	}
}

bool forcejoints = false;

void printlastanim(void)
{
	if (quiet)
		return;

	anim &a = anims[anims.length()-1];
	if (a.numframes == 1)
		printf("    frame %i:\tname=\"%s\"\tfps=%g, %s\n", anims.length()-1,
		       &stringdata[a.name], a.fps, (a.flags & VVM_LOOP)?"looped":"clamped");
	else {
		printf("    anim %i:\tname=\"%s\",\tframes=%i, fps=%g, %s\n", anims.length()-1,
		       &stringdata[a.name], a.numframes, a.fps, (a.flags & VVM_LOOP)?"looped":"clamped");
	}
	loopv(events_fte)
	{
		if (events_fte[i].anim == (uint)anims.length()-1)
			printf("        pose %g: %x \"%s\"\n", events_fte[i].timestamp*a.fps, events_fte[i].evcode, events_fte[i].evdata_str);
	}
}

void printbones(int parent = -1, size_t ind = 1)
{
	char prefix[256];
	if (ind >= sizeof(prefix))
		ind = sizeof(prefix)-1;
	memset(prefix, ' ', ind);
	prefix[ind] = 0;

	loopv(joints)
	{
		if (joints[i].parent == parent)
		{       //show as 1-based for consistency with quake.
			conoutf("%sbone %i:\tname=\"%s\"\tparent=%i, group=%i", prefix, i+1, &stringdata[joints[i].name], joints[i].parent+1, joints[i].group);
			printbones(i, ind+1);
		}
	}
}
void printbonelist()
{
	loopv(joints)
	{
		conoutf("bone %i:\tname=\"%s\"\tparent=%i%s, group=%i", i+1, &stringdata[joints[i].name], joints[i].parent+1, joints[i].parent >= i?"(ERROR)":"", joints[i].group);
	}
}

int findjoint(const char *name)
{
	loopv(joints)
	{
		if (!strcmp(&stringdata[joints[i].name], name))
			return i;
	}
	return -1;
}

bool floatcmp(float f1, float f2)
{
	if (f1 != f2)
		return true;
	return false;
}

void makeanims(const filespec &spec)
{
	if(escale != 1) loopv(eposes) eposes[i].pos *= escale;
	if(erotate != Quat(0, 0, 0, 1)) loopv(ejoints)
		{
			ejoint &ej = ejoints[i];
			if(ej.parent < 0) for(int j = i; j < eposes.length(); j += ejoints.length())
				{
					transform &p = eposes[j];
					p.orient = erotate * p.orient;
					p.pos = erotate.transform(p.pos);
				}
		}
	int numbasejoints = eframes.length() ? eframes[0] : eposes.length();
	if(forcejoints || emeshes.length())
	{
		bool warned = false;
		int *jr = new int[ejoints.length()];
		loopv(ejoints)
		{
			ejoint &ej = ejoints[i];
			jr[i] = findjoint(ej.name);
			if (jr[i] >= 0)
			{
				bool rigmismatch = false;
				if (warned || forcejoints)
					continue;
				joint &j = joints[jr[i]];
				loopk(3) if (floatcmp(j.pos[k], eposes[i].pos[k] + (ej.parent>=0?0:ejointtrans[k]))) rigmismatch = true;
				loopk(4) if (floatcmp(j.orient[k], eposes[i].orient[k])) rigmismatch = true;
				loopk(3) if (floatcmp(j.scale[k], eposes[i].scale[k])) rigmismatch = true;
				if (rigmismatch)
				{
					warned = true;
					conoutf("warning: rig mismatch (bone %s)", ej.name);
				}
				continue;
			}
			jr[i] = joints.length();
			joint &j = joints.add();
			Matrix3x4 &m = mjoints.add();
			const char *name = ej.name;
			int group = -1;
			loopvk(boneoverrides)
			{
				if (!strcmp(boneoverrides[k].name, name))
				{
					boneoverrides[k].used = true;
					if (boneoverrides[k].props.rename)
						name = boneoverrides[k].props.rename;
					if (boneoverrides[k].props.group >= 0)
						group = boneoverrides[k].props.group;
					break;
				}
			}
			j.name = sharestring(name);
			if (ej.parent >= 0)
				j.parent = findjoint(ejoints[ej.parent].name);
			else
				j.parent = -1;
			if (group < 0 && j.parent >= 0)
				group = joints[j.parent].group;
			if (group < 0)
				group = 0;
			j.group = group;
			if(i < numbasejoints)
			{
				m.invert(Matrix3x4(eposes[i].orient, eposes[i].pos + (ej.parent>=0?Vec3(0,0,0):ejointtrans), eposes[i].scale));
				loopk(3) j.pos[k] = eposes[i].pos[k] + (ej.parent>=0?0:ejointtrans[k]);
				loopk(4) j.orient[k] = eposes[i].orient[k];
				loopk(3) j.scale[k] = eposes[i].scale[k];
			}
			else m.invert(Matrix3x4(Quat(0, 0, 0, 1), Vec3(0, 0, 0), Vec3(1, 1, 1)));
			if(j.parent >= 0) m *= mjoints[j.parent];
		}
		loopv(eblends)
		{
			loopk(eblends[i].sorted)
			{
				int b = eblends[i].bones[k];
				if (b >= ejoints.length())
					b = 0;
				else
					b = jr[b];
				eblends[i].bones[k] = b;
			}
		}
		delete[] jr;
	}
//	loopv(spec.events)
//		spec.events[i].evdata_idx = 0;
	loopv(eanims)
	{
		eanim &ea = eanims[i];
		if (ea.flags & VVM_UNPACK)
		{       //some quake mods suck, and are unable to deal with animations
			for (int j = ea.startframe, end = eanims.inrange(i+1) ? eanims[i+1].startframe : eframes.length(); j < end && j < ea.endframe; j++)
			{
				anim &a = anims.add();
				char nname[256];
				formatstring(nname, "%s%i", ea.name, j+1-ea.startframe);
				a.name = sharestring(nname);
				a.firstframe = frames.length();
				a.numframes = 0;
				a.fps = ea.fps;
				a.flags = ea.flags&~VVM_ALLPRIVATE;

				int offset = eframes[j], range = (eframes.inrange(j+1) ? eframes[j+1] : eposes.length()) - offset;
				if(range <= 0) continue;
				frame &fr = frames.add();
				loopk(min(range, ejoints.length())) fr.pose.add(frame::framepose(ejoints[i], eposes[offset + k]));
				loopk(max(ejoints.length() - range, 0)) fr.pose.add(frame::framepose(ejoints[i], transform(Vec3(0, 0, 0), Quat(0, 0, 0, 1), Vec3(1, 1, 1))));
				a.numframes++;

				printlastanim();
			}
		}
		else
		{
			anim &a = anims.add();
			a.name = sharestring(ea.name);
			a.firstframe = frames.length();
			a.numframes = 0;
			a.fps = ea.fps;
			a.flags = ea.flags&~VVM_ALLPRIVATE;

			for(int j = ea.startframe, end = eanims.inrange(i+1) ? eanims[i+1].startframe : eframes.length(); j < end && j <= ea.endframe; j++)
			{
				int offset = eframes[j], range = (eframes.inrange(j+1) ? eframes[j+1] : eposes.length()) - offset;
				if(range <= 0) continue;
				frame &fr = frames.add();
				loopk(min(range, ejoints.length()))
				{
					if (ejoints[k].parent < 0)
						eposes[offset+k].pos += ejointtrans;
					fr.pose.add(frame::framepose(ejoints[k], eposes[offset + k]));
				}
				loopk(max(ejoints.length() - range, 0))
				{
					if (ejoints[k].parent < 0)
						fr.pose.add(frame::framepose(ejoints[k], transform(ejointtrans, Quat(0, 0, 0, 1), Vec3(1, 1, 1))));
					else
						fr.pose.add(frame::framepose(ejoints[k], transform(Vec3(0, 0, 0), Quat(0, 0, 0, 1), Vec3(1, 1, 1))));
				}
				a.numframes++;
			}
			loopvj(spec.events)
			{
				float p;
				if (spec.events[j].anim == ~0u)
				{
					if (spec.events[j].timestamp < ea.startframe || spec.events[j].timestamp >= ea.startframe + a.numframes)
						continue;
					p = spec.events[j].timestamp - ea.startframe;
				}
				else
				{
					if (spec.events[j].anim != (uint)i)
						continue;
					else
						p = spec.events[j].timestamp;
				}
				event_fte &ev = events_fte.add(spec.events[j]);
				ev.anim = anims.length()-1;
				ev.timestamp = p / a.fps;
			}
			printlastanim();
		}
	}

//	loopv(spec.events) if (!spec.events[i].evdata_idx)
//	{
//		conoutf("event specifies invalid animation from %s", spec.file);
//	}
}

bool resetimporter(const filespec &spec, bool reuse = false)
{
	if(reuse)
	{
		ejoints.setsize(0);
		evarrays.setsize(0);

		return false;
	}

	vmap.setsize(0);
	epositions.setsize(0);
	etexcoords.setsize(0);
	enormals.setsize(0);
	etangents.setsize(0);
	ebitangents.setsize(0);
	ecolors.setsize(0);
	loopi(10) ecustom[i].setsize(0);
	eblends.setsize(0);
	etriangles.setsize(0);
	esmoothindexes.setsize(0);
	esmoothedges.setsize(0);
	esmoothgroups.setsize(0);
	esmoothgroups.add();
	ejoints.setsize(0);
	eposes.setsize(0);
	eframes.setsize(0);
	eanims.setsize(0);
	emeshes.setsize(0);
	evarrays.setsize(0);

	emeshtrans = gmeshtrans+spec.translate;
	ejointtrans = spec.translate;
	erotate = spec.rotate;
	escale = gscale*spec.scale;

	return true;
}

bool parseiqe(stream *f)
{
	const char *curmesh = getnamekey(""), *curmaterial = getnamekey("");
	bool needmesh = true;
	int fmoffset = 0;
	char buf[512];
	if(!f->getline(buf, sizeof(buf))) return false;
	if(!strchr(buf, '#') || strstr(buf, "# Inter-Quake Export") != strchr(buf, '#')) return false;
	while(f->getline(buf, sizeof(buf)))
	{
		char *c = buf;
		while(isspace(*c)) ++c;
		if(isalpha(c[0]) && isalnum(c[1]) && (!c[2] || isspace(c[2]))) switch(*c++)
			{
			case 'v':
				switch(*c++)
				{
				case 'p': epositions.add(parseattribs4(c, Vec4(0, 0, 0, 1))); continue;
				case 't': etexcoords.add(parseattribs4(c)); continue;
				case 'n': enormals.add(parseattribs3(c)); continue;
				case 'x':
				{
					Vec4 tangent(parseattribs3(c), 0);
					Vec3 bitangent(0, 0, 0);
					bitangent.x = parseattrib(c);
					if(maybeparseattrib(c, bitangent.y))
					{
						bitangent.z = parseattrib(c);
						ebitangents.add(bitangent);
					}
					else tangent.w = bitangent.x;
					etangents.add(tangent);
					continue;
				}
				case 'b': eblends.add(parseblends(c)); continue;
				case 'c': ecolors.add(parseattribs4(c, Vec4(0, 0, 0, 1))); continue;
				case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
				{
					int n = c[-1] - '0';
					ecustom[n].add(parseattribs4(c));
					continue;
				}
				case 's':
					parseindex(c, esmoothindexes.add());
					continue;
				}
				break;
			case 'p':
			{
				transform t;
				switch(*c++)
				{
				case 'q':
				{
					t.pos = parseattribs3(c);
					loopk(3) t.orient[k] = parseattrib(c);
					t.orient.restorew();
					double w = parseattrib(c, t.orient.w);
					if(w != t.orient.w)
					{
						t.orient.w = w;
						t.orient.normalize();
//        double x2 = f.orient.x*f.orient.x, y2 = f.orient.y*f.orient.y, z2 = f.orient.z*f.orient.z, w2 = f.orient.w*f.orient.w, s2 = x2 + y2 + z2 + w2;
//        f.orient.x = keepsign(f.orient.x, sqrt(max(1.0 - (w2 + y2 + z2) / s2, 0.0)));
//        f.orient.y = keepsign(f.orient.y, sqrt(max(1.0 - (w2 + x2 + z2) / s2, 0.0)));
//        f.orient.z = keepsign(f.orient.z, sqrt(max(1.0 - (w2 + x2 + y2) / s2, 0.0)));
//        f.orient.w = keepsign(f.orient.w, sqrt(max(1.0 - (x2 + y2 + z2) / s2, 0.0)));
					}
					if(t.orient.w > 0) t.orient.flip();
					t.scale = parseattribs3(c, Vec3(1, 1, 1));
					eposes.add(t);
					continue;
				}
				case 'm':
				{
					t.pos = parseattribs3(c);
					Matrix3x3 m;
					m.a = parseattribs3(c);
					m.b = parseattribs3(c);
					m.c = parseattribs3(c);
					Vec3 mscale(Vec3(m.a.x, m.b.x, m.c.x).magnitude(), Vec3(m.a.y, m.b.y, m.c.y).magnitude(), Vec3(m.a.z, m.b.z, m.c.z).magnitude());
					// check determinant for sign of scaling
					if(m.determinant() < 0) mscale = -mscale;
					m.a /= mscale;
					m.b /= mscale;
					m.c /= mscale;
					t.orient = Quat(m);
					if(t.orient.w > 0) t.orient.flip();
					t.scale = parseattribs3(c, Vec3(1, 1, 1)) * mscale;
					eposes.add(t);
					continue;
				}
				case 'a':
				{
					t.pos = parseattribs3(c);
					Vec3 rot = parseattribs3(c);
					t.orient = Quat::fromangles(rot);
					t.scale = parseattribs3(c, Vec3(1, 1, 1));
					eposes.add(t);
					continue;
				}
				}
				break;
			}
			case 'f':
				switch(*c++)
				{
				case 'a':
				{
					int i1 = 0, i2 = 0, i3 = 0;
					if(!parseindex(c, i1) || !parseindex(c, i2)) continue;
					if(needmesh)
					{
						emeshes.add(emesh(curmesh, curmaterial, etriangles.length()));
						needmesh = false;
					}
					if(i1 < 0) i1 = max(epositions.length() + i1, 0);
					if(i2 < 0) i2 = max(epositions.length() + i2, 0);
					while(parseindex(c, i3))
					{
						if(i3 < 0) i3 = max(epositions.length() + i3, 0);
						esmoothgroups.last().flags |= esmoothgroup::F_USED;
						etriangles.add(etriangle(i1, i2, i3, esmoothgroups.length()-1));
						i2 = i3;
					}
					continue;
				}
				case 'm':
				{
					int i1 = 0, i2 = 0, i3 = 0;
					if(!parseindex(c, i1) || !parseindex(c, i2)) continue;
					if(needmesh)
					{
						emeshes.add(emesh(curmesh, curmaterial, etriangles.length()));
						needmesh = false;
					}
					i1 = i1 < 0 ? max(epositions.length() + i1, 0) : (fmoffset + i1);
					i2 = i2 < 0 ? max(epositions.length() + i2, 0) : (fmoffset + i2);
					while(parseindex(c, i3))
					{
						i3 = i3 < 0 ? max(epositions.length() + i3, 0) : (fmoffset + i3);
						esmoothgroups.last().flags |= esmoothgroup::F_USED;
						etriangles.add(etriangle(i1, i2, i3, esmoothgroups.length()-1));
						i2 = i3;
					}
					continue;
				}
				case 's':
				{
					int i1 = 0, i2 = 0, i3 = 0;
					uchar flags = 0;
					if(!parseindex(c, i1) || !parseindex(c, i2) || !parseindex(c, i3)) continue;
					flags |= clamp(i1, 0, 1);
					flags |= clamp(i2, 0, 1)<<1;
					flags |= clamp(i3, 0, 1)<<2;
					esmoothgroups.last().flags |= esmoothgroup::F_USED;
					while(parseindex(c, i3))
					{
						esmoothedges.add(flags | 4);
						flags = 1 | ((flags & 4) >> 1) | (clamp(i3, 0, 1)<<2);
					}
					esmoothedges.add(flags);
					continue;
				}
				}
				break;
			}
		char *args = c;
		while(*args && !isspace(*args)) args++;
		if(!strncmp(c, "smoothgroup", max(int(args-c), 11)))
		{
			if(esmoothgroups.last().flags & esmoothgroup::F_USED) esmoothgroups.dup();
			parseindex(args, esmoothgroups.last().key);
		}
		else if(!strncmp(c, "smoothangle", max(int(args-c), 11)))
		{
			if(esmoothgroups.last().flags & esmoothgroup::F_USED) esmoothgroups.dup();
			double angle = parseattrib(args, 0);
			esmoothgroups.last().angle = fabs(cos(clamp(angle, -180.0, 180.0) * M_PI/180));
		}
		else if(!strncmp(c, "smoothuv", max(int(args-c), 8)))
		{
			if(esmoothgroups.last().flags & esmoothgroup::F_USED) esmoothgroups.dup();
			int val = 1;
			if(parseindex(args, val) && val <= 0) esmoothgroups.last().flags &= ~esmoothgroup::F_UVSMOOTH;
			else esmoothgroups.last().flags |= esmoothgroup::F_UVSMOOTH;
		}
		else if(!strncmp(c, "mesh", max(int(args-c), 4)))
		{
			curmesh = getnamekey(trimname(args));
			if(emeshes.empty() || emeshes.last().name != curmesh) needmesh = true;
			fmoffset = epositions.length();

#if 0
			emesh &m = emeshes.add();
			m.firsttri = etriangles.length();
			fmoffset = epositions.length();
			parsename(args, m.name);
#endif
		}
		else if(!strncmp(c, "material", max(int(args-c), 8)))
		{
			curmaterial = getnamekey(trimname(args));
			if(emeshes.empty() || emeshes.last().material != curmaterial) needmesh = true;
//			if(emeshes.length()) parsename(c, emeshes.last().material);
		}
		else if(!strncmp(c, "joint", max(int(args-c), 5)))
		{
			ejoint &j = ejoints.add();
			j.name = getnamekey(trimname(args));
			parseindex(args, j.parent);
		}
		else if(!strncmp(c, "vertexarray", max(int(args-c), 11)))
		{
			evarray &va = evarrays.add();
			va.type = findvertexarraytype(trimname(args));
			va.format = findvertexarrayformat(trimname(args));
			va.size = strtol(args, &args, 10);
			copystring(va.name, trimname(args));
		}
		else if(!strncmp(c, "animation", max(int(args-c), 9)))
		{
			eanim &a = eanims.add();
			a.name = getnamekey(trimname(args));
			a.startframe = eframes.length();
			if(!eframes.length() || eframes.last() != eposes.length()) eframes.add(eposes.length());
		}
		else if(!strncmp(c, "frame", max(int(args-c), 5)))
		{
			if(eanims.length() && eframes.length() && eframes.last() != eposes.length()) eframes.add(eposes.length());
		}
		else if(!strncmp(c, "framerate", max(int(args-c), 9)))
		{
			if(eanims.length())
			{
				double fps = parseattrib(args);
				eanims.last().fps = max(fps, 0.0);
			}
		}
		else if(!strncmp(c, "loop", max(int(args-c), 4)))
		{
			if(eanims.length()) eanims.last().flags |= VVM_LOOP;
		}
		else if(!strncmp(c, "comment", max(int(args-c), 7)))
		{
			if(commentdata.length()) break;
			for(;;)
			{
				size_t len = f->read(commentdata.reserve(1024), 1024);
				commentdata.advance(len);
				if(len < 1024) { commentdata.add('\0'); break; }
			}
		}
	}

	return true;
}

bool loadiqe(const char *filename, const filespec &spec)
{
	int numfiles = 0;
	while(filename)
	{
		const char *endfile = strchr(filename, ',');
		const char *file = endfile ? newstring(filename, endfile-filename) : filename;
		stream *f = openfile(file, "r");
		if(f)
		{
			resetimporter(spec, numfiles > 0);
			if(parseiqe(f)) numfiles++;
			delete f;
		}

		if(!endfile) break;

		delete[] file;
		filename = endfile+1;
	}

	if(!numfiles) return false;

	if(eanims.length() == 1)
	{
		eanim &a = eanims.last();
		if(spec.name) a.name = spec.name;
		if(spec.fps > 0) a.fps = spec.fps;
		a.flags |= spec.flags;
		if(spec.endframe >= 0) a.endframe = a.startframe + spec.endframe;
		else if(spec.endframe < -1) a.endframe = a.startframe + max(eframes.length() - a.startframe + spec.endframe + 1, 0);
		a.startframe += spec.startframe;
	}

	makeanims(spec);
	if(emeshes.length())
	{
		smoothverts();
		makemeshes(spec);
	}

	return true;
}

struct md5weight
{
	int joint;
	double bias;
	Vec3 pos;
};

struct md5vert
{
	double u, v;
	uint start, count;
};

struct md5hierarchy
{
	const char *name;
	int parent, flags, start;
};

vector<md5weight> weightinfo;
vector<md5vert> vertinfo;

void buildmd5verts()
{
	loopv(vertinfo)
	{
		md5vert &v = vertinfo[i];
		Vec3 pos(0, 0, 0);
		loopk(v.count)
		{
			md5weight &w = weightinfo[v.start+k];
			transform &j = eposes[w.joint];
			pos += (j.orient.transform(w.pos) + j.pos)*w.bias;
		}
		epositions.add(Vec4(pos, 1));
		etexcoords.add(Vec4(v.u, v.v, 0, 0));

		blendcombo &c = eblends.add();
		loopj(v.count)
		{
			md5weight &w = weightinfo[v.start+j];
			c.addweight(w.bias, w.joint);
		}
		c.finalize();
	}
}

void parsemd5mesh(stream *f, char *buf, size_t bufsize)
{
	md5weight w;
	md5vert v;
	etriangle t(0, 0, 0, 0);
	int index, firsttri = etriangles.length(), firstvert = vertinfo.length(), firstweight = weightinfo.length(), numtris = 0, numverts = 0, numweights = 0;
	emesh m;

	while(f->getline(buf, bufsize) && buf[0]!='}')
	{
		if(strstr(buf, "// meshes:"))
		{
			char *start = strchr(buf, ':')+1;
			if(*start==' ') start++;
			char *end = start + strlen(start)-1;
			while(end >= start && isspace(*end)) end--;
			end[1] = '\0';
			m.name = getnamekey(start);
		}
		else if(strstr(buf, "shader"))
		{
			char *start = strchr(buf, '"'), *end = start ? strchr(start+1, '"') : NULL;
			if(start && end)
			{
				*end = '\0';
				m.material = getnamekey(start+1);
			}
		}
		else if(sscanf(buf, " numverts %d", &numverts)==1)
		{
			numverts = max(numverts, 0);
			if(numverts)
			{
				vertinfo.reserve(numverts);
				vertinfo.advance(numverts);
			}
		}
		else if(sscanf(buf, " numtris %d", &numtris)==1)
		{
			numtris = max(numtris, 0);
			if(numtris)
			{
				etriangles.reserve(numtris);
				etriangles.advance(numtris);
			}
			m.firsttri = firsttri;
		}
		else if(sscanf(buf, " numweights %d", &numweights)==1)
		{
			numweights = max(numweights, 0);
			if(numweights)
			{
				weightinfo.reserve(numweights);
				weightinfo.advance(numweights);
			}
		}
		else if(sscanf(buf, " vert %d ( %lf %lf ) %u %u", &index, &v.u, &v.v, &v.start, &v.count)==5)
		{
			if(index>=0 && index<numverts)
			{
				v.start += firstweight;
				vertinfo[firstvert + index] = v;
			}
		}
		else if(sscanf(buf, " tri %d %u %u %u", &index, &t.vert[0], &t.vert[1], &t.vert[2])==4)
		{
			if(index>=0 && index<numtris)
			{
				loopk(3) t.vert[k] += firstvert;
				etriangles[firsttri + index] = t;
			}
		}
		else if(sscanf(buf, " weight %d %d %lf ( %lf %lf %lf ) ", &index, &w.joint, &w.bias, &w.pos.x, &w.pos.y, &w.pos.z)==6)
		{
			if(index>=0 && index<numweights) weightinfo[firstweight + index] = w;
		}
	}

	if(numtris && numverts) emeshes.add(m);
}

bool loadmd5mesh(const char *filename, const filespec &spec)
{
	stream *f = openfile(filename, "r");
	if(!f) return false;

	resetimporter(spec);
	esmoothgroups[0].flags |= esmoothgroup::F_UVSMOOTH;

	char buf[512];
	while(f->getline(buf, sizeof(buf)))
	{
		int tmp;
		if(sscanf(buf, " MD5Version %d", &tmp)==1)
		{
			if(tmp!=10) { delete f; return false; }
		}
		else if(sscanf(buf, " numJoints %d", &tmp)==1)
		{
			if(tmp<1 || (joints.length() && tmp != joints.length())) { delete f; return false; }
		}
		else if(sscanf(buf, " numMeshes %d", &tmp)==1)
		{
			if(tmp<1) { delete f; return false; }
		}
		else if(strstr(buf, "joints {"))
		{
			ejoint j;
			transform p;
			while(f->getline(buf, sizeof(buf)) && buf[0]!='}')
			{
				char *c = buf;
				j.name = getnamekey(trimname(c));
				if(sscanf(c, " %d ( %lf %lf %lf ) ( %lf %lf %lf )",
				          &j.parent, &p.pos.x, &p.pos.y, &p.pos.z,
				          &p.orient.x, &p.orient.y, &p.orient.z)==7)
				{
					p.orient.restorew();
					p.scale = Vec3(1, 1, 1);
					ejoints.add(j);
					eposes.add(p);
				}
			}
		}
		else if(strstr(buf, "mesh {"))
		{
			parsemd5mesh(f, buf, sizeof(buf));
		}
	}

	delete f;

	buildmd5verts();
	makeanims(spec);
	smoothverts();
	makemeshes(spec);
	makerelativebasepose();

	return true;
}

bool loadmd5anim(const char *filename, const filespec &spec)
{
	stream *f = openfile(filename, "r");
	if(!f) return false;

	resetimporter(spec);

	vector<md5hierarchy> hierarchy;
	vector<transform> baseframe;
	int animdatalen = 0, animframes = 0, frameoffset = eposes.length(), firstframe = eframes.length();
	double framerate = 0;
	double *animdata = NULL;
	char buf[512];
	while(f->getline(buf, sizeof(buf)))
	{
		int tmp;
		if(sscanf(buf, " MD5Version %d", &tmp)==1)
		{
			if(tmp!=10) { delete f; return false; }
		}
		else if(sscanf(buf, " numJoints %d", &tmp)==1)
		{
			if(tmp<1) { delete f; return false; }
		}
		else if(sscanf(buf, " numFrames %d", &animframes)==1)
		{
			if(animframes<1) { delete f; return false; }
		}
		else if(sscanf(buf, " frameRate %lf", &framerate)==1);
		else if(sscanf(buf, " numAnimatedComponents %d", &animdatalen)==1)
		{
			if(animdatalen>0) animdata = new double[animdatalen];
		}
		else if(strstr(buf, "bounds {"))
		{
			while(f->getline(buf, sizeof(buf)) && buf[0]!='}');
		}
		else if(strstr(buf, "hierarchy {"))
		{
			while(f->getline(buf, sizeof(buf)) && buf[0]!='}')
			{
				char *c = buf;
				md5hierarchy h;
				h.name = getnamekey(trimname(c));
				if(sscanf(c, " %d %d %d", &h.parent, &h.flags, &h.start)==3)
					hierarchy.add(h);
			}
			if(hierarchy.empty()) { delete f; return false; }
			loopv(hierarchy)
			{
				md5hierarchy &h = hierarchy[i];
				ejoint &j = ejoints.add();
				j.name = h.name;
				j.parent = h.parent;
			}
		}
		else if(strstr(buf, "baseframe {"))
		{
			while(f->getline(buf, sizeof(buf)) && buf[0]!='}')
			{
				transform j;
				if(sscanf(buf, " ( %lf %lf %lf ) ( %lf %lf %lf )", &j.pos.x, &j.pos.y, &j.pos.z, &j.orient.x, &j.orient.y, &j.orient.z)==6)
				{
					j.orient.restorew();
					j.scale = Vec3(1, 1, 1);
					baseframe.add(j);
				}
			}
			if(baseframe.length()!=hierarchy.length()) { delete f; return false; }
			eposes.reserve(animframes*baseframe.length());
			eposes.advance(animframes*baseframe.length());
		}
		else if(sscanf(buf, " frame %d", &tmp)==1)
		{
			for(int numdata = 0; f->getline(buf, sizeof(buf)) && buf[0]!='}';)
			{
				for(char *src = buf, *next = src; numdata < animdatalen; numdata++, src = next)
				{
					animdata[numdata] = strtod(src, &next);
					if(next <= src) break;
				}
			}
			int offset = frameoffset + tmp*baseframe.length();
			eframes.add(offset);
			loopv(baseframe)
			{
				md5hierarchy &h = hierarchy[i];
				transform j = baseframe[i];
				if(h.start < animdatalen && h.flags)
				{
					double *jdata = &animdata[h.start];
					if(h.flags&1) j.pos.x = *jdata++;
					if(h.flags&2) j.pos.y = *jdata++;
					if(h.flags&4) j.pos.z = *jdata++;
					if(h.flags&8) j.orient.x = *jdata++;
					if(h.flags&16) j.orient.y = *jdata++;
					if(h.flags&32) j.orient.z = *jdata++;
					j.orient.restorew();
				}
				eposes[offset + i] = j;
			}
		}
	}

	if(animdata) delete[] animdata;
	delete f;

	eanim &a = eanims.add();
	if(spec.name) a.name = getnamekey(spec.name);
	else
	{
		string name;
		copystring(name, filename);
		char *end = strrchr(name, '.');
		if(end) *end = '\0';
		a.name = getnamekey(name);
	}
	a.startframe = firstframe;
	a.fps = spec.fps > 0 ? spec.fps : framerate;
	a.flags = spec.flags;
	if(spec.endframe >= 0) a.endframe = a.startframe + spec.endframe;
	else if(spec.endframe < -1) a.endframe = a.startframe + max(eframes.length() - a.startframe + spec.endframe + 1, 0);
	a.startframe += spec.startframe;

	makeanims(spec);

	return true;
}

namespace smd
{

bool skipcomment(char *&curbuf)
{
	while(*curbuf && isspace(*curbuf)) curbuf++;
	switch(*curbuf)
	{
	case '#':
	case ';':
	case '\r':
	case '\n':
	case '\0':
		return true;
	case '/':
		if(curbuf[1] == '/') return true;
		break;
	}
	return false;
}

void skipsection(stream *f, char *buf, size_t bufsize)
{
	while(f->getline(buf, bufsize))
	{
		char *curbuf = buf;
		if(skipcomment(curbuf)) continue;
		if(!strncmp(curbuf, "end", 3)) break;
	}
}

void readname(char *&curbuf, char *name, size_t namesize)
{
	char *curname = name;
	while(*curbuf && isspace(*curbuf)) curbuf++;
	bool allowspace = false;
	if(*curbuf == '"') { curbuf++; allowspace = true; }
	while(*curbuf)
	{
		char c = *curbuf++;
		if(c == '"') break;
		if(isspace(c) && !allowspace) break;
		if(curname < &name[namesize-1]) *curname++ = c;
	}
	*curname = '\0';
}

void readnodes(stream *f, char *buf, size_t bufsize)
{
	while(f->getline(buf, bufsize))
	{
		char *curbuf = buf;
		if(skipcomment(curbuf)) continue;
		if(!strncmp(curbuf, "end", 3)) break;
		int id = strtol(curbuf, &curbuf, 10);
		string name;
		readname(curbuf, name, sizeof(name));
		int parent = strtol(curbuf, &curbuf, 10);
		if(id < 0 || id > 255 || parent > 255 || !name[0] || (ejoints.inrange(id) && ejoints[id].name)) continue;
		ejoint j;
		j.name = getnamekey(name);
		j.parent = parent;
		while(ejoints.length() <= id) ejoints.add();
		ejoints[id] = j;
	}
}

void readmaterial(char *&curbuf, char *mat, char *name, size_t matsize)
{
	char *curmat = mat;
	while(*curbuf && isspace(*curbuf)) curbuf++;
	char *ext = NULL;
	while(*curbuf)
	{
		char c = *curbuf++;
		if(isspace(c)) break;
		if(c == '.' && !ext) ext = curmat;
		if(curmat < &mat[matsize-1]) *curmat++ = c;
	}
	*curmat = '\0';
	if(!ext) ext = curmat;
	memcpy(name, mat, ext - mat);
	name[ext - mat] = '\0';
}

void readskeleton(stream *f, char *buf, size_t bufsize)
{
	int frame = -1, firstpose = -1;
	while(f->getline(buf, bufsize))
	{
		char *curbuf = buf;
		if(skipcomment(curbuf)) continue;
		if(sscanf(curbuf, " time %d", &frame) == 1) continue;
		else if(!strncmp(curbuf, "end", 3)) break;
		else if(frame != 0) continue;
		int bone;
		Vec3 pos, rot;
		if(sscanf(curbuf, " %d %lf %lf %lf %lf %lf %lf", &bone, &pos.x, &pos.y, &pos.z, &rot.x, &rot.y, &rot.z) != 7)
			continue;
		if(!ejoints.inrange(bone))
			continue;
		if(firstpose < 0)
		{
			firstpose = eposes.length();
			eposes.reserve(ejoints.length());
			eposes.advance(ejoints.length());
		}
		transform p(pos, Quat::fromangles(rot));
		eposes[firstpose + bone] = p;
	}
}

void readtriangles(stream *f, char *buf, size_t bufsize)
{
	emesh m;
	while(f->getline(buf, bufsize))
	{
		char *curbuf = buf;
		if(skipcomment(curbuf)) continue;
		if(!strncmp(curbuf, "end", 3)) break;
		string name, material;
		readmaterial(curbuf, material, name, sizeof(material));
		if(!m.name || strcmp(m.name, name))
		{
			if(m.name && etriangles.length() > m.firsttri) emeshes.add(m);
			m.name = getnamekey(name);
			m.material = getnamekey(material);
			m.firsttri = etriangles.length();
		}
		Vec4 *pos = epositions.reserve(3) + 2, *tc = etexcoords.reserve(3) + 2;
		Vec3 *norm = enormals.reserve(3) + 2;
		blendcombo *c = eblends.reserve(3) + 2;
		loopi(3)
		{
			char *curbuf;
			do
			{
				if(!f->getline(buf, bufsize)) goto endsection;
				curbuf = buf;
			} while(skipcomment(curbuf));
			int parent = -1, numlinks = 0, len = 0;
			if(sscanf(curbuf, " %d %lf %lf %lf %lf %lf %lf %lf %lf %d%n", &parent, &pos->x, &pos->y, &pos->z, &norm->x, &norm->y, &norm->z, &tc->x, &tc->y, &numlinks, &len) < 9) goto endsection;
			curbuf += len;
			pos->w = 1;
			tc->y = 1 - tc->y;
			tc->z = tc->w = 0;
			c->reset();
			double pweight = 0, tweight = 0;
			for(; numlinks > 0; numlinks--)
			{
				int bone = -1, len = 0;
				double weight = 0;
				if(sscanf(curbuf, " %d %lf%n", &bone, &weight, &len) < 2) break;
				curbuf += len;
				tweight += weight;
				if(bone == parent) pweight += weight;
				else c->addweight(weight, bone);
			}
			if(tweight < 1) pweight += 1 - tweight;
			if(pweight > 0) c->addweight(pweight, parent);
			c->finalize();
			--pos;
			--tc;
			--norm;
			--c;
		}
		etriangle &t = etriangles.add();
		loopi(3) t.vert[i] = epositions.length() + i;
		t.smoothgroup = 0;
		epositions.advance(3);
		enormals.advance(3);
		etexcoords.advance(3);
		eblends.advance(3);
	}
endsection:
	if(m.name && etriangles.length () > m.firsttri) emeshes.add(m);
}

int readframes(stream *f, char *buf, size_t bufsize)
{
	int frame = -1, numframes = 0, lastbone = ejoints.length(), frameoffset = eposes.length();
	while(f->getline(buf, bufsize))
	{
		char *curbuf = buf;
		if(skipcomment(curbuf)) continue;
		int nextframe = -1;
		if(sscanf(curbuf, " time %d", &nextframe) == 1)
		{
			for(; lastbone < ejoints.length(); lastbone++) eposes[frameoffset + frame*ejoints.length() + lastbone] = eposes[frameoffset + lastbone];
			if(nextframe >= numframes)
			{
				eposes.reserve(ejoints.length() * (nextframe + 1 - numframes));
				loopi(nextframe - numframes)
				{
					eframes.add(eposes.length());
					eposes.put(&eposes[frameoffset], ejoints.length());
				}
				eframes.add(eposes.length());
				eposes.advance(ejoints.length());
				numframes = nextframe + 1;
			}
			frame = nextframe;
			lastbone = 0;
			continue;
		}
		else if(!strncmp(curbuf, "end", 3)) break;
		int bone;
		Vec3 pos, rot;
		if(sscanf(curbuf, " %d %lf %lf %lf %lf %lf %lf", &bone, &pos.x, &pos.y, &pos.z, &rot.x, &rot.y, &rot.z) != 7)
			continue;
		if(bone < 0 || bone >= ejoints.length())
			continue;
		for(; lastbone < bone; lastbone++) eposes[frameoffset + frame*ejoints.length() + lastbone] = eposes[frameoffset + lastbone];
		lastbone++;
		transform p(pos, Quat::fromangles(rot));
		eposes[frameoffset + frame*ejoints.length() + bone] = p;
	}
	for(; lastbone < ejoints.length(); lastbone++) eposes[frameoffset + frame*ejoints.length() + lastbone] = eposes[frameoffset + lastbone];
	return numframes;
}

}

bool loadsmd(const char *filename, const filespec &spec)
{
	stream *f = openfile(filename, "r");
	if(!f) return false;

	resetimporter(spec);

	char buf[512];
	int version = -1, firstframe = eframes.length();
	bool hastriangles = false;
	while(f->getline(buf, sizeof(buf)))
	{
		char *curbuf = buf;
		if(smd::skipcomment(curbuf)) continue;
		if(sscanf(curbuf, " version %d", &version) == 1)
		{
			if(version != 1) { delete f; return false; }
		}
		else if(!strncmp(curbuf, "nodes", 5))
			smd::readnodes(f, buf, sizeof(buf));
		else if(!strncmp(curbuf, "triangles", 9))
		{
			smd::readtriangles(f, buf, sizeof(buf));
			hastriangles = true;
		}
		else if(!strncmp(curbuf, "skeleton", 8))
			smd::readframes(f, buf, sizeof(buf));
		else if(!strncmp(curbuf, "vertexanimation", 15))
			smd::skipsection(f, buf, sizeof(buf));
	}

	delete f;

	if(!spec.fps && hastriangles)
	{
		eframes.setsize(firstframe);
		makeanims(spec);
		smoothverts();
		makemeshes(spec);
	}
	else
	{
		eanim &a = eanims.add();
		if(spec.name) a.name = getnamekey(spec.name);
		else
		{
			string name;
			const char *shortname = filename;
			shortname = strrchr(filename, '/');
			if (shortname)
				shortname++;
			else
				shortname = filename;
			copystring(name, shortname);
			char *end = strrchr(name, '.');
			if(end) *end = '\0';
			a.name = getnamekey(name);
		}
		a.startframe = firstframe;
		a.fps = spec.fps;
		a.flags = spec.flags;
		if(spec.endframe >= 0) a.endframe = a.startframe + spec.endframe;
		else if(spec.endframe < -1) a.endframe = a.startframe + max(eframes.length() - a.startframe + spec.endframe + 1, 0);
		a.startframe += spec.startframe;
		makeanims(spec);
	}

	return true;
}

struct objvert { int attrib[3]; objvert() {
			 attrib[0] = attrib[1] = attrib[2] = -1;
		 }
};
static inline uint hthash(const objvert &k) {
	return k.attrib[0] ^ k.attrib[1] ^ k.attrib[2];
};
static inline bool htcmp(const objvert &x, const objvert &y) {
	return x.attrib[0] == y.attrib[0] && x.attrib[1] == y.attrib[1] && x.attrib[2] == y.attrib[2];
}

void parseobjvert(char *s, vector<Vec3> &out)
{
	Vec3 &v = out.add(Vec3(0, 0, 0));
	while(isalpha(*s)) s++;
	loopi(3)
	{
		v[i] = strtod(s, &s);
		while(isspace(*s)) s++;
		if(!*s) break;
	}
}

bool parseobj(stream *f)
{
	vector<Vec3> attrib[3];
	char buf[512];
	hashtable<objvert, int> verthash;
	string meshname = "", matname = "";
	int curmesh = -1, smooth = 0;

	while(f->getline(buf, sizeof(buf)))
	{
		char *c = buf;
		while(isspace(*c)) c++;
		switch(*c)
		{
		case '#': continue;
		case 'v':
			if(isspace(c[1])) parseobjvert(c, attrib[0]);
			else if(c[1]=='t') parseobjvert(c, attrib[1]);
			else if(c[1]=='n') parseobjvert(c, attrib[2]);
			break;
		case 'g':
		{
			while(isalpha(*c)) c++;
			while(isspace(*c)) c++;
			char *name = c;
			size_t namelen = strlen(name);
			while(namelen > 0 && isspace(name[namelen-1])) namelen--;
			copystring(meshname, name, min(namelen+1, sizeof(meshname)));
			curmesh = -1;
			break;
		}
		case 'u':
		{
			if(!strncmp(c, "usemtl", 6)) continue;
			while(isalpha(*c)) c++;
			while(isspace(*c)) c++;
			char *name = c;
			size_t namelen = strlen(name);
			while(namelen > 0 && isspace(name[namelen-1])) namelen--;
			copystring(matname, name, min(namelen+1, sizeof(matname)));
			curmesh = -1;
			break;
		}
		case 's':
		{
			if(!isspace(c[1])) continue;
			while(isalpha(*c)) c++;
			while(isspace(*c)) c++;
			int key = strtol(c, &c, 10);
			smooth = -1;
			loopv(esmoothgroups) if(esmoothgroups[i].key == key) { smooth = i; break; }
			if(smooth < 0)
			{
				smooth = esmoothgroups.length();
				esmoothgroups.add().key = key;
			}
			break;
		}
		case 'f':
		{
			if(curmesh < 0)
			{
				emesh m;
				m.name = getnamekey(meshname);
				m.material = getnamekey(matname);
				m.firsttri = etriangles.length();
				curmesh = emeshes.length();
				emeshes.add(m);
				verthash.clear();
			}
			int v0 = -1, v1 = -1;
			while(isalpha(*c)) c++;
			for(;;)
			{
				while(isspace(*c)) c++;
				if(!*c) break;
				objvert vkey;
				loopi(3)
				{
					vkey.attrib[i] = strtol(c, &c, 10);
					if(vkey.attrib[i] < 0) vkey.attrib[i] = attrib[i].length() + vkey.attrib[i];
					else vkey.attrib[i]--;
					if(!attrib[i].inrange(vkey.attrib[i])) vkey.attrib[i] = -1;
					if(*c!='/') break;
					c++;
				}
				int *index = verthash.access(vkey);
				if(!index)
				{
					index = &verthash[vkey];
					*index = epositions.length();
					epositions.add(Vec4(vkey.attrib[0] < 0 ? Vec3(0, 0, 0) : attrib[0][vkey.attrib[0]].zxy(), 1));
					if(vkey.attrib[2] >= 0) enormals.add(attrib[2][vkey.attrib[2]].zxy());
					etexcoords.add(vkey.attrib[1] < 0 ? Vec4(0, 0, 0, 0) : Vec4(attrib[1][vkey.attrib[1]].x, 1-attrib[1][vkey.attrib[1]].y, 0, 0));
				}
				if(v0 < 0) v0 = *index;
				else if(v1 < 0) v1 = *index;
				else
				{
					etriangles.add(etriangle(*index, v1, v0, smooth));
					v1 = *index;
				}
			}
			break;
		}
		}
	}

	return true;
}

bool loadobj(const char *filename, const filespec &spec)
{
	stream *f = openfile(filename, "r");
	if(!f) return false;

	int numfiles = 0;
	while(filename)
	{
		const char *endfile = strchr(filename, ',');
		const char *file = endfile ? newstring(filename, endfile-filename) : filename;
		stream *f = openfile(file, "r");
		if(f)
		{
			if(resetimporter(spec, numfiles > 0))
			{
				esmoothgroups[0].key = 0;
			}
			if(parseobj(f)) numfiles++;
			delete f;
		}

		if(!endfile) break;

		delete[] file;
		filename = endfile+1;
	}

	if(!numfiles) return false;

	smoothverts();
	makemeshes(spec);

	return true;
}

namespace fbx
{
struct token
{
	enum { NONE, PROP, NUMBER, STRING, ARRAY, BEGIN, END, LINE };
	int type;
	union
	{
		char s[64];
		double f;
		int i;
	};

	token() : type(NONE) {
	}
};

struct tokenizer
{
	stream *f;
	char *pos;
	char buf[4096];

	void reset(stream *s) {
		f = s; pos = buf; buf[0] = '\0';
	}

	bool parse(token &t)
	{
		for(;;)
		{
			while(isspace(*pos)) pos++;
			if(!*pos)
			{
				bool more = f->getline(buf, sizeof(buf));
				pos = buf;
				if(!more) { buf[0] = '\0'; return false; }
				t.type = token::LINE;
				return true;
			}
			size_t slen = 0;
			switch(*pos)
			{
			case ',':
				pos++;
				continue;
			case ';':
				pos++;
				while(*pos) pos++;
				continue;
			case '{':
				pos++;
				t.type = token::BEGIN;
				return true;
			case '}':
				pos++;
				t.type = token::END;
				return true;
			case '"':
				pos++;
				for(; *pos && *pos != '"'; pos++) if(slen < sizeof(t.s)-1) t.s[slen++] = *pos;
				t.s[slen] = '\0';
				if(*pos == '"') pos++;
				t.type = token::STRING;
				return true;
			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': case '.': case '-': case '+':
				t.f = strtod(pos, &pos);
				t.type = token::NUMBER;
				return true;
			case '*':
				pos++;
				t.i = int(strtol(pos, &pos, 10));
				t.type = token::ARRAY;
				return true;
			default:
				for(; *pos && !isspace(*pos) && *pos != ':'; pos++) if(slen < sizeof(t.s)-1) t.s[slen++] = *pos;
				t.s[slen] = '\0';
				if(*pos == ':') pos++;
				t.type = token::PROP;
				return true;
			}
		}
		return false;
	}


	bool skipprop()
	{
		token t;
		while(parse(t)) switch(t.type)
			{
			case token::LINE:
				return true;

			case token::BEGIN:
				while(parse(t)) switch(t.type)
					{
					case token::PROP:
						skipprop();
						break;
					case token::END:
						return true;
					}
				return true;

			case token::END:
				return false;
			}
		return false;
	}

	bool findbegin()
	{
		token t;
		while(parse(t)) switch(t.type)
			{
			case token::LINE: return false;
			case token::BEGIN: return true;
			}
		return false;
	}

	template<class T>
	bool readarray(vector<T> &vals, int size = 0)
	{
		if(!findbegin()) return false;

		if(size > 0) vals.reserve(min(size, 1<<16));
		token t;
		while(parse(t)) switch(t.type)
			{
			case token::NUMBER: if(size <= 0 || vals.length() < size) vals.add(T(t.f)); break;
			case token::END: return true;
			}
		return false;
	}
};

struct node
{
	enum { GEOM = 0, MODEL, MATERIAL, LIMB, CLUSTER, SKIN, CURVE, XFORM, ANIMLAYER, ANIMSTACK };
	enum { TRANS = 0, ROT, SCALE };

	virtual int type() = 0;
	virtual ~node() {
	}

	virtual void process() {
	}
	virtual void finish() {
	}
};

struct namednode : node
{
	string name;

	namednode() {
		name[0] = 0;
	}
};

struct geomnode;
struct modelnode;
struct materialnode;
struct limbnode;
struct clusternode;
struct skinnode;
struct curvenode;
struct xformnode;
struct animlayernode;
struct animstacknode;

struct geomnode : node
{
	int mesh, firstvert, lastvert, numverts;
	modelnode *model;
	vector<int> remap;
	vector<blendcombo> blends;

	geomnode() : mesh(-1), firstvert(-1), lastvert(-1), numverts(0), model(NULL) {
	}
	int type() {
		return GEOM;
	}

	void process();
	void finish();
};

struct modelnode : namednode
{
	materialnode *material;
	Vec3 geomtrans, prerot, lcltrans, lclrot, lclscale;

	modelnode() : material(NULL), geomtrans(0, 0, 0), prerot(0, 0, 0), lcltrans(0, 0, 0), lclrot(0, 0, 0), lclscale(1, 1, 1) {
	}

	int type() {
		return MODEL;
	}
};

struct materialnode : namednode
{
	int type() {
		return MATERIAL;
	}
};

struct limbnode : namednode
{
	limbnode *parent;
	int index;
	Vec3 trans, rot, prerot, scale;
	clusternode *cluster;

	limbnode() : parent(NULL), index(-1), trans(0, 0, 0), rot(0, 0, 0), prerot(0, 0, 0), scale(1, 1, 1), cluster(NULL) {
	}

	int type() {
		return LIMB;
	}

	void process()
	{
		if(parent) ejoints[index].parent = parent->index;
	}

	void finish();
};

struct clusternode : node
{
	skinnode *skin;
	limbnode *limb;
	vector<int> indexes;
	vector<double> weights, transform, transformlink;

	clusternode() : skin(NULL), limb(NULL) {
	}

	int type() {
		return CLUSTER;
	}

	void process();
};

struct skinnode : node
{
	geomnode *geom;

	skinnode() : geom(NULL) {
	}

	int type() {
		return SKIN;
	}
};

struct curvenode : node
{
	vector<double> vals;

	int type() {
		return CURVE;
	}

	bool varies() const
	{
		loopv(vals) if(vals[i] != vals[0]) return true;
		return false;
	}
};

struct xformnode : node
{
	limbnode *limb;
	int xform;
	Vec3 val;
	curvenode *curves[3];

	xformnode() : limb(NULL), xform(-1), val(0, 0, 0) {
		curves[0] = curves[1] = curves[2] = NULL;
	}

	void setcurve(int i, curvenode *c)
	{
		if(c->varies()) curves[i] = c;
		else if(c->vals.length()) val[i] = c->vals[0];
	}

	int numframes()
	{
		int n = 0;
		loopi(3) if(curves[i]) { if(!n) n = curves[i]->vals.length(); else if(n != curves[i]->vals.length()) n = -1; }
		return n;
	}

	int type() {
		return XFORM;
	}
};

struct animlayernode : namednode
{
	vector<xformnode *> xforms;

	int numframes()
	{
		int n = 0;
		loopv(xforms)
		{
			int xn = xforms[i]->numframes();
			if(xn) { if(!n) n = xn; else if(n != xn) n = -1; }
		}
		return n;
	}

	int type() {
		return ANIMLAYER;
	}
};

struct animstacknode : namednode
{
	vector<animlayernode *> layers;
	double secs;

	animstacknode() : secs(0) {
	}

	int numframes()
	{
		int n;
		loopv(layers)
		{
			int ln = layers[i]->numframes();
			if(ln) { if(!n) n = ln; else if(n != ln) n = -1; }
		}
		return n;
	}

	int type() {
		return ANIMSTACK;
	}

	void process();
};

hashtable<double, node *> nodes;
tokenizer p;

void parsegeometry()
{
	token t;
	if(!p.parse(t)) return;
	if(t.type != token::NUMBER) { p.skipprop(); return; }
	double id = t.f;
	if(!p.findbegin()) return;

	vector<double> verts, norms, uvs, colors;
	vector<int> polyidxs, uvidxs, coloridxs;
	while(p.parse(t)) switch(t.type)
		{
		case token::END:
			goto endgeometry;

		case token::PROP:
			if(!strcmp(t.s, "Vertices")) p.readarray(verts);
			else if(!strcmp(t.s, "PolygonVertexIndex")) p.readarray(polyidxs);
			else if(!strcmp(t.s, "LayerElementNormal"))
			{
				if(p.findbegin())
				{
					while(p.parse(t)) switch(t.type)
						{
						case token::PROP:
							if(!strcmp(t.s, "Normals")) p.readarray(norms);
							else p.skipprop();
							break;
						case token::END:
							goto endnormals;
						}
endnormals:                             ;
				}
			}
			else if(!strcmp(t.s, "LayerElementUV"))
			{
				if(p.findbegin())
				{
					while(p.parse(t)) switch(t.type)
						{
						case token::PROP:
							if(!strcmp(t.s, "UV")) p.readarray(uvs);
							else if(!strcmp(t.s, "UVIndex")) p.readarray(uvidxs);
							else p.skipprop();
							break;
						case token::END:
							goto enduvs;
						}
enduvs:                                 ;
				}
			}
			else if(!strcmp(t.s, "LayerElementColor"))
			{
				if(p.findbegin())
				{
					while(p.parse(t)) switch(t.type)
						{
						case token::PROP:
							if(!strcmp(t.s, "Colors")) p.readarray(colors);
							else if(!strcmp(t.s, "ColorIndex")) p.readarray(coloridxs);
							else p.skipprop();
							break;
						case token::END:
							goto endcolors;
						}
endcolors:                              ;
				}
			}
			else p.skipprop();
			break;
		}
endgeometry:
	int poslen = epositions.length();
	geomnode *n = new geomnode;
	nodes[id] = n;
	if(polyidxs.empty()) for(int i = 0; i + 2 < verts.length(); i += 3) epositions.add(Vec4(verts[i], verts[i+1], verts[i+2], 1));
	else
	{
		loopv(polyidxs)
		{
			int idx = polyidxs[i];
			if(idx < 0) idx = -(idx+1);
			n->remap.add(idx);
			idx *= 3;
			epositions.add(Vec4(verts[idx], verts[idx+1], verts[idx+2], 1));
		}
	}
	loopi(epositions.length() - poslen) eblends.add();

	emesh m;
	m.name = getnamekey("");
	m.material = getnamekey("");
	m.firsttri = etriangles.length();
	for(int i = poslen; i + 2 < epositions.length(); i += 3)
		etriangles.add(etriangle(i+1, i, i+2));
	emeshes.add(m);

	n->mesh = emeshes.length()-1;
	n->firstvert = poslen;
	n->lastvert = epositions.length();
	n->numverts = verts.length()/3;

	if(uvidxs.empty())
	{
		if(polyidxs.length() && uvs.length()/2 == verts.length()/3) loopv(polyidxs)
			{
				int idx = polyidxs[i];
				if(idx < 0) idx = -(idx+1);
				idx *= 2;
				etexcoords.add(Vec4(uvs[idx], 1-uvs[idx+1], 0, 0));
			}
		else for(int i = 0; i + 1 < uvs.length(); i += 2) etexcoords.add(Vec4(uvs[i], 1-uvs[i+1], 0, 0));
	}
	else loopv(uvidxs)
		{
			int idx = 2*uvidxs[i];
			etexcoords.add(Vec4(uvs[idx], 1-uvs[idx+1], 0, 0));
		}

	if(polyidxs.length() && norms.length() == verts.length()) loopv(polyidxs)
		{
			int idx = polyidxs[i];
			if(idx < 0) idx = -(idx+1);
			idx *= 3;
			enormals.add(Vec3(norms[idx], norms[idx+1], norms[idx+2]));
		}
	else for(int i = 0; i + 2 < norms.length(); i += 3) enormals.add(Vec3(norms[i], norms[i+1], norms[i+2]));

	if(coloridxs.empty())
	{
		if(polyidxs.length() && colors.length()/4 == verts.length()/3) loopv(polyidxs)
			{
				int idx = polyidxs[i];
				if(idx < 0) idx = -(idx+1);
				idx *= 4;
				ecolors.add(Vec4(colors[idx], colors[idx+1], colors[idx+2], colors[idx+3]));
			}
		else for(int i = 0; i + 3 < colors.length(); i += 4) ecolors.add(Vec4(colors[i], colors[i+1], colors[i+2], colors[i+3]));
	}
	else loopv(coloridxs)
		{
			int idx = 4*coloridxs[i];
			ecolors.add(Vec4(colors[idx], colors[idx+1], colors[idx+2], colors[idx+3]));
		}
}

void parsemodel()
{
	token id, name, type, t;
	if(!p.parse(id) || !p.parse(name) || !p.parse(type)) return;

	if(id.type != token::NUMBER || type.type != token::STRING || name.type != token::STRING) { p.skipprop(); return; }

	char *str = name.s;
	if(strstr(str, "Model::") == str) str += strlen("Model::");
	if(!strcmp(type.s, "Mesh"))
	{
		modelnode *n = new modelnode;
		copystring(n->name, str);
		nodes[id.f] = n;

		if(!p.findbegin()) return;
		while(p.parse(t)) switch(t.type)
			{
			case token::END: return;
			case token::PROP:
				if(!strcmp(t.s, "Properties70"))
				{
					if(!p.findbegin()) return;
					while(p.parse(t)) switch(t.type)
						{
						case token::END: goto endmeshprops;
						case token::PROP:
							if(!strcmp(t.s, "P"))
							{
								if(!p.parse(t)) return;
								if(t.type == token::STRING)
								{
									if(!strcmp(t.s, "PreRotation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->prerot[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "GeometricTranslation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->geomtrans[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "Lcl Translation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->lcltrans[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "Lcl Rotation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->lclrot[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "Lcl Scaling"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->lclscale[i] = t.f;
										}
									}
								}
							}
							p.skipprop();
							break;
						}
endmeshprops:                           ;
				}
				p.skipprop();
				break;
			}
	}
	else if(!strcmp(type.s, "LimbNode"))
	{
		limbnode *n = new limbnode;
		copystring(n->name, str);
		n->index = ejoints.length();
		nodes[id.f] = n;

		ejoint &j = ejoints.add();
		j.name = getnamekey(str);
		j.parent = -1;
		eposes.add(transform(Vec3(0, 0, 0), Quat(0, 0, 0, 1)));

		if(!p.findbegin()) return;
		while(p.parse(t)) switch(t.type)
			{
			case token::END:
			{
				transform &x = eposes[n->index];
				x.pos = n->trans;
				x.orient = Quat::fromdegrees(n->rot);
				x.scale = n->scale;
			}
				return;
			case token::PROP:
				if(!strcmp(t.s, "Properties70"))
				{
					if(!p.findbegin()) return;
					while(p.parse(t)) switch(t.type)
						{
						case token::END: goto endlimbprops;
						case token::PROP:
							if(!strcmp(t.s, "P"))
							{
								if(!p.parse(t)) return;
								if(t.type == token::STRING)
								{
									if(!strcmp(t.s, "PreRotation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->prerot[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "Lcl Translation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->trans[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "Lcl Rotation"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->rot[i] = t.f;
										}
									}
									else if(!strcmp(t.s, "Lcl Scaling"))
									{
										loopi(3) if(!p.parse(t)) return;
										loopi(3) {
											if(!p.parse(t)) return; if(t.type != token::NUMBER) break; n->scale[i] = t.f;
										}
									}
								}
							}
							p.skipprop();
							break;
						}
endlimbprops:                           ;
				}
				p.skipprop();
				break;
			}
	}

	p.skipprop();
}

void parsematerial()
{
	token id, name;
	if(!p.parse(id) || !p.parse(name)) return;

	if(id.type == token::NUMBER)
	{
		if(name.type == token::STRING)
		{
			char *str = name.s;
			if(strstr(str, "Material::") == str) str += strlen("Material::");
			materialnode *n = new materialnode;
			copystring(n->name, str);
			nodes[id.f] = n;
		}
	}

	p.skipprop();
}

void parsedeformer()
{
	token id, name, type, t;
	if(!p.parse(id) || !p.parse(name) || !p.parse(type)) return;
	if(id.type != token::NUMBER || type.type != token::STRING || name.type != token::STRING)
	{
		p.skipprop();
		return;
	}

	if(!strcmp(type.s, "Skin"))
	{
		skinnode *n = new skinnode;
		nodes[id.f] = n;
	}
	else if(!strcmp(type.s, "Cluster"))
	{
		if(!p.findbegin()) return;

		clusternode *n = new clusternode;
		nodes[id.f] = n;
		while(p.parse(t)) switch(t.type)
			{
			case token::END: return;
			case token::PROP:
				if(!strcmp(t.s, "Indexes")) p.readarray(n->indexes);
				else if(!strcmp(t.s, "Weights")) p.readarray(n->weights);
				else if(!strcmp(t.s, "Transform")) p.readarray(n->transform);
				else if(!strcmp(t.s, "TransformLink")) p.readarray(n->transformlink);
				else p.skipprop();
				break;
			}
		return;
	}

	p.skipprop();
}

void parsecurve()
{
	token id, t;
	if(!p.parse(id)) return;
	if(id.type != token::NUMBER) { p.skipprop(); return; }
	curvenode *n = new curvenode;
	nodes[id.f] = n;
	while(p.parse(t)) switch(t.type)
		{
		case token::END: return;
		case token::PROP:
			if(!strcmp(t.s, "KeyValueFloat")) p.readarray(n->vals);
			else p.skipprop();
			break;
		}
}

void parsexform()
{
	token id, t;
	if(!p.parse(id)) return;
	if(id.type != token::NUMBER) { p.skipprop(); return; }
	if(!p.findbegin()) return;
	xformnode *n = new xformnode;
	nodes[id.f] = n;
	while(p.parse(t)) switch(t.type)
		{
		case token::END: return;
		case token::PROP:
			if(!strcmp(t.s, "Properties70"))
			{
				if(!p.findbegin()) return;
				while(p.parse(t)) switch(t.type)
					{
					case token::END: goto endprops;
					case token::PROP:
						if(!strcmp(t.s, "P"))
						{
							token name, type, val;
							if(!p.parse(name) || !p.parse(type) || !p.parse(t) || !p.parse(t)) return;
							if(name.type == token::STRING)
							{
								if(!strcmp(name.s, "d|X")) { if(p.parse(val) && val.type == token::NUMBER) n->val.x = val.f; }
								else if(!strcmp(name.s, "d|Y")) { if(p.parse(val) && val.type == token::NUMBER) n->val.y = val.f; }
								else if(!strcmp(name.s, "d|Z")) { if(p.parse(val) && val.type == token::NUMBER) n->val.z = val.f; }
							}
						}
						p.skipprop();
						break;
					}
endprops:                       ;
			}
			else p.skipprop();
			break;
		}
}

void parseanimlayer()
{
	token id, name;
	if(!p.parse(id) || !p.parse(name)) return;
	if(id.type != token::NUMBER || name.type != token::STRING) { p.skipprop(); return; }

	char *str = name.s;
	if(strstr(str, "AnimLayer::") == str) str += strlen("AnimLayer::");
	animlayernode *n = new animlayernode;
	copystring(n->name, str);
	nodes[id.f] = n;

	p.skipprop();
}

	#define FBX_SEC 46186158000.0

void parseanimstack()
{
	token id, name, t;
	if(!p.parse(id) || !p.parse(name)) return;
	if(id.type != token::NUMBER || name.type != token::STRING) { p.skipprop(); return; }

	char *str = name.s;
	if(strstr(str, "AnimStack::") == str) str += strlen("AnimStack::");
	animstacknode *n = new animstacknode;
	copystring(n->name, str);
	nodes[id.f] = n;

	if(!p.findbegin()) return;
	while(p.parse(t)) switch(t.type)
		{
		case token::END: return;
		case token::PROP:
			if(!strcmp(t.s, "Properties70"))
			{
				if(!p.findbegin()) return;
				while(p.parse(t)) switch(t.type)
					{
					case token::END: goto endprops;
					case token::PROP:
						if(!strcmp(t.s, "P"))
						{
							token name, type, val;
							if(!p.parse(name) || !p.parse(type) || !p.parse(t) || !p.parse(t)) return;
							if(name.type == token::STRING)
							{
								if(!strcmp(name.s, "LocalStop")) { if(p.parse(val) && val.type == token::NUMBER) n->secs = val.f / FBX_SEC; }
							}
						}
						p.skipprop();
						break;
					}
endprops:                       ;
			}
			else p.skipprop();
			break;
		}
}

void parseobjects()
{
	if(!p.findbegin()) return;

	token t;
	while(p.parse(t)) switch(t.type)
		{
		case token::END:
			return;

		case token::PROP:
			if(!strcmp(t.s, "Geometry")) parsegeometry();
			else if(!strcmp(t.s, "Model")) parsemodel();
			else if(!strcmp(t.s, "Material")) parsematerial();
			else if(!strcmp(t.s, "Deformer")) parsedeformer();
			else if(!strcmp(t.s, "AnimationCurve")) parsecurve();
			else if(!strcmp(t.s, "AnimationCurveNode")) parsexform();
			else if(!strcmp(t.s, "AnimationLayer")) parseanimlayer();
			else if(!strcmp(t.s, "AnimationStack")) parseanimstack();
			else p.skipprop();
			break;
		}
}

void parseconnection()
{
	token type, from, to, prop;
	if(!p.parse(type) || !p.parse(from) || !p.parse(to)) return;
	if(type.type == token::STRING && from.type == token::NUMBER && to.type == token::NUMBER)
	{
		node *nf = nodes.find(from.f, NULL), *nt = nodes.find(to.f, NULL);
		if(!strcmp(type.s, "OO") && nf && nt)
		{
			if(nf->type() == node::GEOM && nt->type() == node::MODEL)
				((geomnode *)nf)->model = (modelnode *)nt;
			else if(nf->type() == node::MATERIAL && nt->type() == node::MODEL)
				((modelnode *)nt)->material = (materialnode *)nf;
			else if(nf->type() == node::LIMB && nt->type() == node::LIMB)
				((limbnode *)nf)->parent = (limbnode *)nt;
			else if(nf->type() == node::CLUSTER && nt->type() == node::SKIN)
				((clusternode *)nf)->skin = (skinnode *)nt;
			else if(nf->type() == node::SKIN && nt->type() == node::GEOM)
				((skinnode *)nf)->geom = (geomnode *)nt;
			else if(nf->type() == node::LIMB && nt->type() == node::CLUSTER)
			{
				((clusternode *)nt)->limb = (limbnode *)nf;
				((limbnode *)nf)->cluster = (clusternode *)nt;
			}
			else if(nf->type() == node::ANIMLAYER && nt->type() == node::ANIMSTACK)
				((animstacknode *)nt)->layers.add((animlayernode *)nf);
			else if(nf->type() == node::XFORM && nt->type() == node::ANIMLAYER)
				((animlayernode *)nt)->xforms.add((xformnode *)nf);
		}
		else if(!strcmp(type.s, "OP") && nf && nt && p.parse(prop) && prop.type == token::STRING)
		{
			if(nf->type() == node::CURVE && nt->type() == node::XFORM)
			{
				if(!strcmp(prop.s, "d|X")) ((xformnode *)nt)->setcurve(0, (curvenode *)nf);
				else if(!strcmp(prop.s, "d|Y")) ((xformnode *)nt)->setcurve(1, (curvenode *)nf);
				else if(!strcmp(prop.s, "d|Z")) ((xformnode *)nt)->setcurve(2, (curvenode *)nf);
			}
			else if(nf->type() == node::XFORM && nt->type() == node::LIMB)
			{
				((xformnode *)nf)->limb = (limbnode *)nt;
				if(!strcmp(prop.s, "Lcl Translation")) ((xformnode *)nf)->xform = xformnode::TRANS;
				else if(!strcmp(prop.s, "Lcl Rotation")) ((xformnode *)nf)->xform = xformnode::ROT;
				else if(!strcmp(prop.s, "Lcl Scaling")) ((xformnode *)nf)->xform = xformnode::SCALE;
			}
		}
	}
	p.skipprop();
}

void parseconnections()
{
	if(!p.findbegin()) return;

	token t;
	while(p.parse(t)) switch(t.type)
		{
		case token::END:
			return;

		case token::PROP:
			if(!strcmp(t.s, "C")) parseconnection();
			else p.skipprop();
			break;
		}
}

void geomnode::process()
{
	if(model)
	{
		emeshes[mesh].name = getnamekey(model->name);
		if(model->material) emeshes[mesh].material = getnamekey(model->material->name);
		if(model->geomtrans != Vec3(0, 0, 0)) for(int i = firstvert; i < lastvert; i++) epositions[i] += model->geomtrans;
		if(model->lclscale != Vec3(1, 1, 1))
		{
			for(int i = firstvert; i < lastvert; i++)
			{
				epositions[i].setxyz(model->lclscale * Vec3(epositions[i]));
			}
		}
		if(model->lclrot != Vec3(0, 0, 0))
		{
			Quat lclquat = Quat::fromdegrees(model->lclrot);
			for(int i = firstvert; i < lastvert; i++)
			{
				epositions[i].setxyz(lclquat.transform(Vec3(epositions[i])));
				enormals[i] = lclquat.transform(enormals[i]);
			}
		}
		if(model->prerot != Vec3(0, 0, 0))
		{
			Quat prequat = Quat::fromdegrees(model->prerot);
			for(int i = firstvert; i < lastvert; i++)
			{
				epositions[i].setxyz(prequat.transform(Vec3(epositions[i])));
				enormals[i] = prequat.transform(enormals[i]);
			}
		}
		if(model->lcltrans != Vec3(0, 0, 0)) for(int i = firstvert; i < lastvert; i++) epositions[i] += model->lcltrans;
	}
}

void clusternode::process()
{
	if(!limb || limb->index > 255 || !skin || !skin->geom || indexes.length() != weights.length()) return;
	geomnode *g = skin->geom;
	if(g->blends.empty()) loopi(g->numverts) g->blends.add();
	loopv(indexes)
	{
		int idx = indexes[i];
		double weight = weights[i];
		g->blends[idx].addweight(weight, limb->index);
	}
}

void animstacknode::process()
{
	if(layers.empty()) return;
	animlayernode *l = layers[0];
	int numframes = l->numframes();
	if(numframes < 0) return;

	eanim &a = eanims.add();
	a.name = getnamekey(name);
	a.startframe = eframes.length();
	a.fps = secs > 0 ? numframes/secs : 0;

	transform *poses = eposes.reserve(numframes*ejoints.length());
	loopj(numframes)
	{
		eframes.add(eposes.length());
		eposes.put(eposes.getbuf(), ejoints.length());
	}
	loopv(l->xforms)
	{
		xformnode &x = *l->xforms[i];
		if(!x.limb) continue;
		transform *dst = &poses[x.limb->index];
		loopj(numframes)
		{
			Vec3 val = x.val;
			loopk(3) if(x.curves[k]) val[k] = x.curves[k]->vals[j];
			switch(x.xform)
			{
			case xformnode::TRANS: dst->pos = val; break;
			case xformnode::ROT: dst->orient = Quat::fromdegrees(val); break;
			case xformnode::SCALE: dst->scale = val; break;
			}
			dst += ejoints.length();
		}
	}
#if 0
	loopv(eposes)
	{
		transform &t = eposes[i];
		Matrix3x3 m(t.orient, t.scale);
		Vec3 mscale(Vec3(m.a.x, m.b.x, m.c.x).magnitude(), Vec3(m.a.y, m.b.y, m.c.y).magnitude(), Vec3(m.a.z, m.b.z, m.c.z).magnitude());
		if(m.determinant() < 0) mscale = -mscale;
		m.a /= mscale;
		m.b /= mscale;
		m.c /= mscale;
		Quat morient(m); if(morient.w > 0) morient.flip();
		t.orient = morient;
		t.scale = mscale;
	}
#endif
}

void geomnode::finish()
{
	if(blends.empty()) return;

	loopv(blends) blends[i].finalize();
	while(eblends.length() < lastvert) eblends.add();
	if(remap.length()) loopv(remap) eblends[firstvert + i] = blends[remap[i]];
	else loopv(blends) eblends[firstvert + i] = blends[i];
}

void limbnode::finish()
{
	if(prerot == Vec3(0, 0, 0)) return;
	Quat prequat = Quat::fromdegrees(prerot);
	for(int i = index; i < eposes.length(); i += ejoints.length())
		eposes[i].orient = prequat * eposes[i].orient;
}

bool checkversion(stream *f)
{
	return f->getline(p.buf, sizeof(p.buf)) && strstr(p.buf, "FBX 7");
}

void parse(stream *f)
{
	p.reset(f);
	token t;
	while(p.parse(t)) switch(t.type)
		{
		case token::PROP:
			if(!strcmp(t.s, "Objects")) parseobjects();
			else if(!strcmp(t.s, "Connections")) parseconnections();
			else p.skipprop();
			break;
		}
	enumerate(nodes, double, id, node *, n, { (void)id; n->process(); });
	enumerate(nodes, double, id, node *, n, { (void)id; n->finish(); });
	enumerate(nodes, double, id, node *, n, { (void)id; delete n; });
	nodes.clear();
}
}

bool loadfbx(const char *filename, const filespec &spec)
{
	int numfiles = 0;
	while(filename)
	{
		const char *endfile = strchr(filename, ',');
		const char *file = endfile ? newstring(filename, endfile-filename) : filename;
		stream *f = openfile(file, "r");
		if(f)
		{
			if(fbx::checkversion(f))
			{
				resetimporter(spec, numfiles > 0);
				numfiles++;
				fbx::parse(f);
			}
			delete f;
		}

		if(!endfile) break;

		delete[] file;
		filename = endfile+1;
	}

	if(!numfiles) return false;

	if(eanims.length() == 1)
	{
		eanim &a = eanims.last();
		if(spec.name) a.name = spec.name;
		if(spec.fps > 0) a.fps = spec.fps;
		a.flags |= spec.flags;
		if(spec.endframe >= 0) a.endframe = a.startframe + spec.endframe;
		else if(spec.endframe < -1) a.endframe = a.startframe + max(eframes.length() - a.startframe + spec.endframe + 1, 0);
		a.startframe += spec.startframe;
	}

	erotate *= Quat(M_PI/2, Vec3(1, 0, 0));

	makeanims(spec);
	if(emeshes.length())
	{
		smoothverts();
		makemeshes(spec);
	}

	return true;
}



void genhitboxes(vector<hitbox> &hitboxes)
{
	//for half-life weenies that are too lazy to define their own hitmeshes
	if (!hitboxes.length())
		return;

	filespec inspec;
	inspec.reset();
	resetimporter(inspec);
	loopv(hitboxes)
	{
		hitbox &hb = hitboxes[i];
		int bone = -1;
		for (bone = 0; bone < joints.length(); bone++)
			if (!strcasecmp(hb.bone, &stringdata[joints[bone].name]))
				break;
		if (bone == joints.length())
		{
			fatal("error: hitbox attached to invalid bone %s", hb.bone);
			continue;       //this hitbox is invalid
		}

		emesh &m = emeshes.add();
		int firstvert = epositions.length();
		m.firsttri = etriangles.length();
		m.material = "textures/common/hitmesh"; //to be vaugely compatible with q3map2's default shader names
		string tmp;
		formatstring(tmp, "hitbox%i", hitboxes[i].body);
		m.name = newstring(tmp);
		m.hasexplicits = true;
		memset(&m.explicits, 0, sizeof(m.explicits));
		m.explicits.contents = 0x02000000;
		m.explicits.surfaceflags = 0x80;
		m.explicits.body = hitboxes[i].body;
		m.explicits.geomset = ~0u;

		//spit out some verts
		for (int j = 0; j < 8; j++)
		{
			Vec3 p = Vec3((j&1)?hb.mins[0]:hb.maxs[0], (j&2)?hb.mins[1]:hb.maxs[1], (j&4)?hb.mins[2]:hb.maxs[2]);
			Matrix3x4 bm(mjoints[bone]);
			bm.invert();
			p = bm.transform(p);
			epositions.add(Vec4(p, 0));
			enormals.add(p);
			etexcoords.add(Vec4(0,0,0,0));
			eblends.add(blendcombo()).addweight(1, bone);
		}

		//and some triangles for them
		etriangles.add(etriangle(firstvert+2, firstvert+1, firstvert+0));
		etriangles.add(etriangle(firstvert+2, firstvert+3, firstvert+1));
		etriangles.add(etriangle(firstvert+4, firstvert+5, firstvert+6));
		etriangles.add(etriangle(firstvert+5, firstvert+7, firstvert+6));

		etriangles.add(etriangle(firstvert+0, firstvert+1, firstvert+4));
		etriangles.add(etriangle(firstvert+1, firstvert+5, firstvert+4));
		etriangles.add(etriangle(firstvert+6, firstvert+3, firstvert+2));
		etriangles.add(etriangle(firstvert+6, firstvert+7, firstvert+3));

		etriangles.add(etriangle(firstvert+4, firstvert+2, firstvert+0));
		etriangles.add(etriangle(firstvert+4, firstvert+6, firstvert+2));
		etriangles.add(etriangle(firstvert+1, firstvert+3, firstvert+5));
		etriangles.add(etriangle(firstvert+3, firstvert+7, firstvert+5));
	}
	smoothverts();
	makemeshes(inspec);
}

int framesize = 0;
vector<ushort> animdata;

#define QUANTIZE(offset, base, scale) ushort(0.5f + (float(offset) - base) / scale)

static int jsort(const void *va, const void *vb)
{
	joint &a = joints[*(int*)va];
	joint &b = joints[*(int*)vb];

	if (a.group == b.group)
	{
		if (*(int*)va > *(int*)vb)
			return 1;
		else
			return -1;
	}
	else if (a.group < b.group)
		return -1;
	else
		return 1;
}
void calcanimdata()
{
	hashtable<const char *, bool> bonewarnings;

	//reorder the joints according to their groups, including a lookup so we can fix up other mappings
	int *jointremap = new int[joints.length()];
	int *jointremapinv = new int[joints.length()];
	loopv(joints) jointremap[i] = i;
	qsort(jointremap, joints.length(), sizeof(int), jsort);
	vector<joint> oj;
	joints.swap(oj);
	bool dodgyorder = false;
	loopv(oj)
	jointremapinv[jointremap[i]] = i;
	loopv(oj)
	{
		joint &j = joints.add(oj[jointremap[i]]);
		if (j.parent >= 0)
		{
			j.parent = jointremapinv[j.parent];
			if (j.parent >= i)
				dodgyorder = true;
		}
	}
	if (dodgyorder)
	{
		printbonelist();
		fatal("Bone group reordering resulted in invalid order");
	}

	//try and ensure that the animation bone order matches the mesh bones
	loopv(joints)
	{
		pose &j = poses.add();
		j.name = &stringdata[joints[i].name];
		j.parent = joints[i].parent;
		loopk(10) {
			j.offset[k] = 1e16f; j.scale[k] = -1e16f;
		}
	}

	loopv(frames)
	{
		frame &fr = frames[i];
		loopl(fr.pose.length())
		{
			frame::framepose &p = fr.pose[l];
			p.remap = -1;
			loopvk(poses)
			{
//				if (poses[k].parent == p.boneparent)
				if (!strcmp(poses[k].name, p.bonename))
				{
					if (poses[k].parent == -1 || p.boneparent == -1)
					{
						if (poses[k].parent != -1 || p.boneparent != -1)
							fatal("Error: bone %s has inconsistent parents\n", p.bonename);
					}
					else if (strcmp(poses[poses[k].parent].name, fr.pose[p.boneparent].bonename))
						fatal("Error: bone %s has inconsistent parents (%s vs %s)\n", p.bonename, poses[poses[k].parent].name, fr.pose[p.boneparent].bonename);
					p.remap = k;
					break;
				}
			}
			if(p.remap < 0)
			{
				//if we have a mesh, then any extra bones are surplus to requirements.
				//otherwise we play safe and keep all (which is kinda awkward, because there's no way to name them in the output vvm).
				if (!joints.empty())
				{
					if (!bonewarnings.find(p.bonename, false))
					{
						const char *a = "UNKNOWN";
						loopvj(anims)
						{
							if ((uint)i >= anims[j].firstframe && (uint)i < anims[j].firstframe+anims[j].numframes)
							{
								a = &stringdata[anims[j].name];
								break;
							}
						}
						bonewarnings.access(p.bonename, true);
						if (p.boneparent >= 0)
							conoutf("warning: ignoring bone %s (parent %s) (surplus in %s)", p.bonename, fr.pose[p.boneparent].bonename, a);
						else
							conoutf("warning: ignoring bone %s (root) (surplus in %s)", p.bonename, a);
					}
					continue;
				}
				if (p.boneparent >= 0)
					conoutf("bone %s (%s)", p.bonename, poses[p.boneparent].name);
				else
					conoutf("bone %s", p.bonename);
				p.remap = poses.length();
				pose &j = poses.add();
				j.name = p.bonename;
				j.parent = -1;
				loopk(p.remap)
				{
					if (!strcmp(poses[k].name, fr.pose[p.boneparent].bonename))
					{
						j.parent = k;
						break;
					}
				}
				loopk(10) {
					j.offset[k] = 1e16f; j.scale[k] = -1e16f;
				}
			}

			pose &j = poses[p.remap];
			transform &f = p.tr;
			loopk(3)
			{
				j.offset[k] = min(j.offset[k], float(f.pos[k]));
				j.scale[k] = max(j.scale[k], float(f.pos[k]));
			}
			loopk(4)
			{
				j.offset[3+k] = min(j.offset[3+k], float(f.orient[k]));
				j.scale[3+k] = max(j.scale[3+k], float(f.orient[k]));
			}
			loopk(3)
			{
				j.offset[7+k] = min(j.offset[7+k], float(f.scale[k]));
				j.scale[7+k] = max(j.scale[7+k], float(f.scale[k]));
			}
		}
	}
	loopv(poses)
	{
		pose &j = poses[i];
		loopk(10)
		{
			j.scale[k] -= j.offset[k];
			if(j.scale[k] >= 1e-10f) { framesize++; j.scale[k] /= 0xFFFF; j.flags |= 1<<k; }
			else j.scale[k] = 0.0f;
		}
	}
#if 0
	int runlength = 0, blocksize = 0, blocks = 0;
	#define FLUSHVAL(val) \
	if(!blocksize || (animdata.last() == val ? runlength >= 0xFF : runlength || blocksize > 0xFF)) \
	{ \
		animdata.add(0); \
		animdata.add(val); \
		blocksize = 1; \
		runlength = 0; \
		blocks++; \
	} \
	else if(animdata.last() == val) \
	{ \
		animdata[animdata.length()-blocksize-1] += 0x10; \
		runlength++; \
	} \
	else \
	{ \
		animdata[animdata.length()-blocksize-1]++; \
		animdata.add(val); \
		blocksize++; \
	}
	loopv(joints)
	{
		joint &j = joints[i];
		loopk(3) if(j.flags & (0x01<<k))
		{
			for(int l = i; l < frames.length(); l += poses.length())
			{
				transform &f = frames[l];
				ushort val = QUANTIZE(f.pos[k], j.offset[k], j.scale[k]);
				FLUSHVAL(val);
			}
		}
		loopk(4) if(j.flags & (0x08<<k))
		{
			for(int l = i; l < frames.length(); l += poses.length())
			{
				transform &f = frames[l];
				ushort val = QUANTIZE(f.orient[k], j.offset[3+k], j.scale[3+k]);
				FLUSHVAL(val);
			}
		}
		loopk(3) if(j.flags & (0x80<<k))
		{
			for(int l = i; l < frames.length(); l += poses.length())
			{
				transform &f = frames[l];
				ushort val = QUANTIZE(f.scale[k], j.offset[7+k], j.scale[7+k]);
				FLUSHVAL(val);
			}
		}
	}
	printf("%d frames of size %d/%d compressed from %d/%d to %d in %d blocks", frames.length()/poses.length(), framesize, poses.length()*9, framesize*frames.length()/poses.length(), poses.length()*9*frames.length()/poses.length(), animdata.length(), blocks);
#else
	transform *tr = new transform[poses.length()];
	char *def = new char[poses.length()];
	loopvk(poses) {
		def[k]=0;
	}
	loopv(frames)
	{
		frame &fr = frames[i];
		loopvk(poses) {
			tr[k] = transform(Vec3(0,0,0),Quat(0,0,0,1)); def[k]|=1;
		}
		loopvk(fr.pose) if (fr.pose[k].remap>=0) {tr[fr.pose[k].remap] = fr.pose[k].tr; def[fr.pose[k].remap] &= ~1;}
		loopvk(poses)
		{
			if (def[k] == 1)
			{       //if this bone didn't have any data and is still in an identity pose, warn about it.
				def[k] |= 2;
				const char *a = "UNKNOWN";
				loopvj(anims)
				{
					if ((uint)i >= anims[j].firstframe && (uint)i < anims[j].firstframe+anims[j].numframes)
					{
						a = &stringdata[anims[j].name];
						break;
					}
				}
				conoutf("warning: bone %s defaulted (missing in %s)", poses[k].name, a);
			}
			pose &j = poses[k];
			transform &f = tr[k];
			loopk(3) if(j.flags & (0x01<<k)) animdata.add(QUANTIZE(f.pos[k], j.offset[k], j.scale[k]));
			loopk(4) if(j.flags & (0x08<<k)) animdata.add(QUANTIZE(f.orient[k], j.offset[3+k], j.scale[3+k]));
			loopk(3) if(j.flags & (0x80<<k)) animdata.add(QUANTIZE(f.scale[k], j.offset[7+k], j.scale[7+k]));
		}
	}
	delete[] tr;
#endif

	//combine the arrays into a single vertex data lump.
	loopv(varrays)
	{
		vertexarray &va = varrays[i];
		va.offset = vdata.length();

		//align it, if needed
		uint align = max(va.formatsize(), 8);
		if(va.offset%align) { uint pad = align - va.offset%align; va.offset += pad; loopi(pad) vdata.add(0); }

		//splurge the data out.
		uchar *final = vdata.reserve(va.bytesize() * va.count);
		uchar *src = &va.vdata[0];
		if (va.type == VVM_BLENDINDEXES && (va.format == VVM_UBYTE || va.format == VVM_BYTE))
			loopk(va.size*va.count) final[k] = jointremapinv[src[k]];
		else if (va.type == VVM_BLENDINDEXES && (va.format == VVM_USHORT || va.format == VVM_SHORT))
			loopk(va.size*va.count) ((ushort*)final)[k] = jointremapinv[((ushort*)src)[k]];
		else if (va.type == VVM_BLENDINDEXES && (va.format == VVM_UINT || va.format == VVM_INT))
			loopk(va.size*va.count) ((uint*)final)[k] = jointremapinv[((uint*)src)[k]];
		else
			memcpy(final, &va.vdata[0], va.bytesize() * va.count);
		vdata.advance(va.bytesize() * va.count);

		va.vdata.setsize(0);    //no longer needed.
	}

	while(vdata.length()%4) vdata.add(0);
	while(stringdata.length()%4) stringdata.add('\0');
	while(commentdata.length()%4) commentdata.add('\0');
	while(animdata.length()%2) animdata.add(0);

	delete[] jointremap;
	delete[] jointremapinv;

	if (joints.length()) loopv(frames) makebounds(bounds.add(), mjoints.getbuf(), frames[i]);
}

bool writevvm(const char *filename)
{
	vector<vvmextension> extensions;

	stream *f = openfile(filename, "wb");
	if(!f) return false;

	vvmheader hdr;
	memset(&hdr, 0, sizeof(hdr));
	copystring(hdr.magic, VVM_MAGIC, sizeof(hdr.magic));
	hdr.version = VVM_VERSION;
	hdr.filesize = sizeof(hdr);
	hdr.flags = modelflags;

	vvmextension *ext_meshes_fte = NULL;
	if (meshes_fte.length())
	{
		ext_meshes_fte = &extensions.add();
		ext_meshes_fte->name = sharestring("FTE_MESH");
	}
	vvmextension *ext_events_fte = NULL;
	if (events_fte.length())
	{
		ext_events_fte = &extensions.add();
		ext_events_fte->name = sharestring("FTE_EVENT");

		loopv(events_fte)
		{
			event_fte &ev = events_fte[i];
			ev.evdata_idx = sharestring(ev.evdata_str);
		}
	}

	if(stringdata.length()) hdr.ofs_text = hdr.filesize; hdr.num_text = stringdata.length(); hdr.filesize += hdr.num_text;
	hdr.num_meshes = meshes.length(); if(meshes.length()) hdr.ofs_meshes = hdr.filesize; hdr.filesize += meshes.length() * sizeof(vvmmesh);
	uint voffset = hdr.filesize + varrays.length() * sizeof(vvmvertexarray);
	hdr.num_vertexarrays = varrays.length(); if(varrays.length()) hdr.ofs_vertexarrays = hdr.filesize; hdr.filesize += varrays.length() * sizeof(vvmvertexarray);
	uint valign = (8 - (hdr.filesize%8))%8;
	voffset += valign;
	hdr.filesize += valign + vdata.length();
	hdr.num_vertexes = numfverts;
	hdr.num_triangles = triangles.length(); if(triangles.length()) hdr.ofs_triangles = hdr.filesize; hdr.filesize += triangles.length() * sizeof(vvmtriangle);
	if(neighbors.length()) hdr.ofs_adjacency = hdr.filesize; hdr.filesize += neighbors.length() * sizeof(vvmtriangle);
	hdr.num_joints = joints.length(); if(joints.length()) hdr.ofs_joints = hdr.filesize; hdr.filesize += joints.length() * sizeof(vvmjoint);
	hdr.num_poses = poses.length(); if(poses.length()) hdr.ofs_poses = hdr.filesize; hdr.filesize += poses.length() * sizeof(vvmpose);
	hdr.num_anims = anims.length(); if(anims.length()) hdr.ofs_anims = hdr.filesize; hdr.filesize += anims.length() * sizeof(vvmanim);
	hdr.num_frames = frames.length(); hdr.num_framechannels = framesize;
	if(animdata.length()) hdr.ofs_frames = hdr.filesize; hdr.filesize += animdata.length() * sizeof(ushort);
	if(bounds.length()) hdr.ofs_bounds = hdr.filesize; hdr.filesize += bounds.length() * sizeof(float[8]);
	if(commentdata.length()) hdr.ofs_comment = hdr.filesize; hdr.num_comment = commentdata.length(); hdr.filesize += hdr.num_comment;
	if (extensions.length()) hdr.ofs_extensions = hdr.filesize; hdr.num_extensions = extensions.length(); hdr.filesize += sizeof(vvmextension) * hdr.num_extensions;
	if (ext_meshes_fte) {ext_meshes_fte->ofs_data = hdr.filesize; ext_meshes_fte->num_data = meshes_fte.length()*sizeof(vvmext_fte_mesh); hdr.filesize += ext_meshes_fte->num_data;}
	if (ext_events_fte) {ext_events_fte->ofs_data = hdr.filesize; ext_events_fte->num_data = events_fte.length()*sizeof(vvmext_fte_events); hdr.filesize += ext_events_fte->num_data;}

	lilswap(&hdr.version, (sizeof(hdr) - sizeof(hdr.magic))/sizeof(uint));

	f->write(&hdr, sizeof(hdr));

	if(stringdata.length()) f->write(stringdata.getbuf(), stringdata.length());

	loopv(meshes)
	{
		mesh &m = meshes[i];
		f->putlil(m.name);
		f->putlil(m.material);
		f->putlil(m.firstvert);
		f->putlil(m.numverts);
		f->putlil(m.firsttri);
		f->putlil(m.numtris);
	}

	loopv(varrays)
	{
		vertexarray &v = varrays[i];
		f->putlil(v.type);
		f->putlil(v.flags);
		f->putlil(v.format);
		f->putlil(v.size);
		f->putlil(voffset + v.offset);
	}

	loopi(valign) f->putchar(0);
	f->write(vdata.getbuf(), vdata.length());

	loopv(triangles)
	{
		triangle &t = triangles[i];
		loopk(3) f->putlil(t.vert[k]);
	}

	loopv(neighbors)
	{
		triangle &t = neighbors[i];
		loopk(3) f->putlil(t.vert[k]);
	}

	loopv(joints)
	{
		joint &j = joints[i];
		f->putlil(j.name);
		f->putlil(j.parent);
		loopk(3) f->putlil(float(j.pos[k]));
		loopk(4) f->putlil(float(j.orient[k]));
		loopk(3) f->putlil(float(j.scale[k]));
	}

	loopv(poses)
	{
		pose &p = poses[i];
		f->putlil(p.parent);
		f->putlil(p.flags);
		loopk(10) f->putlil(p.offset[k]);
		loopk(10) f->putlil(p.scale[k]);
	}

	loopv(anims)
	{
		anim &a = anims[i];
		f->putlil(a.name);
		f->putlil(a.firstframe);
		f->putlil(a.numframes);
		f->putlil(a.fps);
		f->putlil(a.flags);
	}

	loopv(animdata) f->putlil(animdata[i]);

	loopv(bounds)
	{
		framebounds &b = bounds[i];
		loopk(3) f->putlil(float(b.bbmin[k]));
		loopk(3) f->putlil(float(b.bbmax[k]));
		f->putlil(float(b.xyradius));
		f->putlil(float(b.radius));
	}

	if(commentdata.length()) f->write(commentdata.getbuf(), commentdata.length());

	loopv (extensions)
	{
		vvmextension &ext = extensions[i];
		f->putlil(ext.name);
		f->putlil(ext.num_data);
		f->putlil(ext.ofs_data);
		if (i == extensions.length()-1)
			f->putlil(0);
		else
			f->putlil((uint)(hdr.ofs_extensions + (i+1)*sizeof(ext)));
	}

	if (ext_meshes_fte) loopv(meshes_fte)
		{
			meshprop &mf = meshes_fte[i];
			f->putlil(mf.contents);
			f->putlil(mf.surfaceflags);
			f->putlil(mf.body);
			f->putlil(mf.geomset);
			f->putlil(mf.geomid);
			f->putlil(mf.mindist);
			f->putlil(mf.maxdist);
		}

	if (ext_events_fte) loopv(events_fte)
		{
			event_fte &ev = events_fte[i];
			f->putlil(ev.anim);
			f->putlil(ev.timestamp);
			f->putlil(ev.evcode);
			f->putlil(ev.evdata_idx);
		}

	delete f;
	return true;
}


void help(bool exitstatus = EXIT_SUCCESS)
{
	fprintf(exitstatus != EXIT_SUCCESS ? stderr : stdout,
	        "Usage:\n"
	        "\n"
	        "./vvm cmdfile.cmd\n"
	        "./vvm [options] output.vvm mesh.iqe anim1.iqe ... animN.iqe\n"
	        "./vvm [options] output.vvm mesh.md5mesh anim1.md5anim ... animN.md5anim\n"
	        "./vvm [options] output.vvm mesh.smd anim1.smd ... animN.smd\n"
	        "./vvm [options] output.vvm mesh.fbx anim1.fbx ... animN.fbx\n"
	        "./vvm [options] output.vvm mesh.obj\n"
	        "\n"
	        "For certain formats, IQE, OBJ, and FBX, it is possible to combine multiple mesh\n"
	        "files of the exact same vertex layout and skeleton by supplying them as\n"
	        "\"mesh1.iqe,mesh2.iqe,mesh3.iqe\", that is, a comma-separated list of the mesh\n"
	        "files (with no spaces) in place of the usual mesh filename.\n"
	        "\n"
	        "Options can be any of the following command-line switches:\n"
	        "\n"
	        "    -s N\n"
	        "    --scale N\n"
	        "      Sets the output scale to N (float).\n"
	        "\n"
	        "    --meshtrans Z\n"
	        "    --meshtrans X,Y,Z\n"
	        "      Translates a mesh by X,Y,Z (floats). This does not affect the skeleton.\n"
	        "\n"
	        "    -j\n"
	        "    --forcejoints\n"
	        "      Forces the exporting of joint information in animation files without\n"
	        "      meshes.\n"
	        "\n"
	        "    -q\n"
	        "      Quiet. Only display warnings or errors.\n"
	        "\n"
	        "    -v\n"
	        "      Verbose. Print lots of extra info.\n"
	        "\n"
	        "    -n\n"
	        "      No Extensions. Disables the use of fte-specific vvm extensions.\n"
	        "\n"
	        "Each animation file can be preceded by any combination of the following command-\n"
	        "line switches:\n"
	        "\n"
	        "    --name A\n"
	        "      Sets the name of the animation to A.\n"
	        "    --fps N\n"
	        "      Sets the FPS of the animation to N (float).\n"
	        "    --loop\n"
	        "      Sets the loop flag for the animation.\n"
	        "    --start N\n"
	        "      Sets the first frame of the animation to N (integer).\n"
	        "    --end N\n"
	        "      Sets the last frame of the animation to N (integer).\n"
	        "\n"
	        "You can supply either a mesh file, animation files, or both.\n"
	        "Note that if an input mesh file is supplied, it must come before the animation\n"
	        "files in the file list.\n"
	        "The output VVM file will contain the supplied mesh and any supplied animations.\n"
	        "If no mesh is provided,the VVM file will simply contain the supplied animations.\n"
	        );
	exit(exitstatus);
}

struct bitnames
{
	const char *std;
	const char *name;
	unsigned int bits;
};

//chops up the input string, returning subsections of it, like strtok_r, except with more specific separators.
char *mystrtok(char **ctx)
{
	char *ret = NULL;
	char *p = *ctx;
	//skip whitespace
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (!*p)
		return NULL;    //eof
	if (*p == '\"')
	{
		ret = ++p;
		while (*p && *p != '\"')
			p++;
		if (*p)
			*p++ = '\0';
		*ctx = p;
	}
	else
	{
		ret = p;
		//we're screwed if we reach a quote without trailing whitespace, blame the user in that case.
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
		*ctx = p;
	}
	return ret;
}

unsigned int parsebits(bitnames *names, char **line)
{
	unsigned int bits = 0;
	char *comma;
	for (char *value = mystrtok(line); value; value = comma)
	{
		comma = strchr(value, ',');
		if (comma)
			*comma++ = 0;

		char *end;
		strtoul(value, &end, 0);
		if (end && !*end)
			bits |= strtoul(value, NULL, 0);
		else
		{
			size_t i;
			char *std = value;
			value = strchr(value, '_');
			if (value)
				*value++ = 0;
			else
			{
				value = std;
				std += strlen(std);
			}
			for (i = 0; names[i].name; i++)
			{
				if (!*std || !strcasecmp(names[i].std, std))
				{
					if (!strcasecmp(names[i].name, value))
					{
						bits |= names[i].bits;
						break;
					}
				}
			}
			if (!names[i].name)
			{       //stuff with no specific standard, mostly for consistency
				if (!*names[i].std)
				{
					if (!strcasecmp(names[i].name, value))
					{
						bits |= names[i].bits;
						break;
					}
				}
				if (!names[i].name)
					fatal("Unknown bit name: %s\n", value);
			}
		}
	}
	return bits;
}

bool parsebonefield(const char *tok, char **line, boneoverride::prop &spec, bool defaults)
{
	if (!strcasecmp(tok, "rename"))
		spec.rename = newstring(mystrtok(line));
	else if (!strcasecmp(tok, "group"))
		spec.group = atoi(mystrtok(line));
	else
		return false;
	return true;
}

bool parsemeshfield(const char *tok, char **line, meshprop &spec, bool defaults)
{
	if (!strcasecmp(tok, "contents"))
	{
		//these should be (mostly) compatible with q2+q3
		bitnames contentnames[] = {
			{"",    "empty",                0x00000000},
			{"",    "solid",                0x00000001},
			{"",    "lava",                 0x00000008},
			{"",    "slime",                0x00000010},
			{"",    "water",                0x00000020},
			{"",    "fluid",                0x00000038},
			{"fte", "ladder",               0x00004000},
			{"",    "playerclip",   0x00010000},
			{"",    "monsterclip",  0x00010000},
			{"",    "body",                 0x02000000},
			{"",    "corpse",               0x04000000},
			{"q2",  "ladder",               0x20000000},
			{"fte", "sky",                  0x80000000},{"q3",      "nodrop",               0x80000000},
			{NULL}
		};
		spec.contents = parsebits(contentnames, line);
	}
	else if (!strcasecmp(tok, "surfaceflags"))
	{
		bitnames surfaceflagnames[] = {
			{"fte", "nodraw",		0x00000080},{"q3",      "nodraw",               0x00000080},
			{"",    "alien",		0x10000000},
			{"",    "flesh",		0x20000000},
			{"",    "foliage",		0x30000000},
			{"",    "computer",		0x40000000},
			{"",    "dirt",			0x50000000},
			{"",    "vent",			0x60000000},
			{"",    "grate",		0x70000000},
			{"",    "metal",		0x80000000},
			{"",    "glass",		0x90000000},
			{"",    "sand",			0xA0000000},
			{"",    "slosh",		0xB0000000},
			{"",    "snow",			0xC0000000},
			{"",    "tile",			0xD0000000},
			{"",    "wood",			0xE0000000},
			{"",    "concrete",		0xF0000000},
			{NULL}
		};
		spec.surfaceflags = parsebits(surfaceflagnames, line);
	}
	else if (!strcasecmp(tok, "body")) {
		spec.body = strtoul(mystrtok(line), NULL, 0);
	}
	else if (!strcasecmp(tok, "geomset"))
	{
		spec.geomset = strtoul(mystrtok(line), NULL, 0);
		spec.geomid = strtoul(mystrtok(line), NULL, 0);
	}
	else if (!strcasecmp(tok, "lodrange"))
	{
		spec.mindist = atof(mystrtok(line));
		spec.maxdist = atof(mystrtok(line));
	}
	else
		return false;

	ext_detected = true;
	return true;
}

bool parseanimfield(const char *tok, char **line, filespec &spec, bool defaults)
{
	if (!strcasecmp(tok, "name") && !defaults)
		spec.name = newstring(mystrtok(line));
	else if (!strcasecmp(tok, "fps"))
		spec.fps = atof(mystrtok(line));
	else if (!strcasecmp(tok, "loop"))
		spec.flags |= VVM_LOOP;
	else if (!strcasecmp(tok, "clamp"))
		spec.flags &= ~VVM_LOOP;
	else if (!strcasecmp(tok, "unpack"))
		spec.flags |= VVM_UNPACK;
	else if (!strcasecmp(tok, "pack"))
		spec.flags &= ~VVM_UNPACK;
	else if (!strcasecmp(tok, "nomesh"))
		spec.nomesh = !!strtoul(mystrtok(line), NULL, 0);
	else if (!strcasecmp(tok, "noanim"))
		spec.noanim = !!strtoul(mystrtok(line), NULL, 0);
	else if (!strcasecmp(tok, "materialprefix"))
		spec.materialprefix = newstring(mystrtok(line));
	else if(!strcasecmp(tok, "start"))
		spec.startframe = max(atoi(mystrtok(line)), 0);
	else if(!strcasecmp(tok, "end"))
		spec.endframe = atoi(mystrtok(line));
	else if (!strcasecmp(tok, "rotate"))
	{
		Vec3 ang;
		ang.x = atof(mystrtok(line))* -M_PI/180;
		ang.z = atof(mystrtok(line))* -M_PI/180;
		ang.y = atof(mystrtok(line))* -M_PI/180;
		spec.rotate = Quat::fromangles(ang);
	}
	else if (!strcasecmp(tok, "scale"))
		spec.scale = atof(mystrtok(line));
	else if (!strcasecmp(tok, "origin"))
	{
		spec.translate.x = atof(mystrtok(line));
		spec.translate.y = atof(mystrtok(line));
		spec.translate.z = atof(mystrtok(line));
	}
	else if (!strcasecmp(tok, "event"))
	{
		const char *poseidx = mystrtok(line);
		char *dot;
		if (!strcmp(poseidx, "reset"))
		{
			spec.events.setsize(0);
			return true;
		}
		event_fte &ev = spec.events.add();
		ev.anim = strtod(poseidx, &dot);
		if (*dot == ':')
			ev.timestamp = strtoul(dot+1, NULL, 0);
		else
		{
			ev.timestamp = strtod(poseidx, &dot);
			ev.anim = ~0u;  //fix up according to poses...
		}
		ev.evcode = atoi(mystrtok(line));
		ev.evdata_str = newstring(mystrtok(line));
		ext_detected = true;
	}
	else if (parsemeshfield(tok, line, spec.meshprops, defaults))
		;
	else
		return false;

	return true;
}

void parsecommands(char *filename, const char *&outfile, vector<filespec> &infiles, vector<hitbox> &hitboxes)
{
	filespec defaultspec;
	defaultspec.reset();

	if (!quiet)
		conoutf("execing %s", filename);

	stream *f = openfile(filename, "rt");
	if(!f)
	{
		fatal("Couldn't open command-file \"%s\"\n", filename);
		return;
	}

	char buf[2048];
	while(f->getline(buf, sizeof(buf)))
	{
		char *tok;
		char *line = buf;
		tok = mystrtok(&line);
		if (tok && *tok == '$')
			tok++;
		if (!tok)
			continue;
		else if (*tok == '#' || !strncasecmp(tok, "//", 2))
		{       //comments
			while (mystrtok(&line))
				;
		}
//		else if (!strcasecmp(tok, "outputdir"))
//			(void)mystrtok(&line);
		else if (!strcasecmp(tok, "output"))
			outfile = newstring(mystrtok(&line));
		else if (!strcasecmp(tok, "hitbox") || !strcasecmp(tok, "hbox"))
		{
			hitbox &hb = hitboxes.add();
			hb.body = strtoul(mystrtok(&line), NULL, 0);
			hb.bone = newstring(mystrtok(&line));
			for (int i = 0; i < 3; i++)
				hb.mins[i] = atof(mystrtok(&line));
			for (int i = 0; i < 3; i++)
				hb.maxs[i] = atof(mystrtok(&line));
		}
		else if (!strcasecmp(tok, "exec"))
			parsecommands(mystrtok(&line), outfile, infiles, hitboxes);
		else if (!strcasecmp(tok, "modelflags"))
		{
			bitnames modelflagnames[] = {
				{"q1",  "rocket",       0x01},
				{"q1",  "grenade",      0x02},
				{"q1",  "gib",          0x04},
				{"q1",  "rotate",       0x08},
				{"q1",  "tracer1",      0x10},
				{"q1",  "zomgib",       0x20},
				{"q1",  "tracer2",      0x40},
				{"q1",  "tracer3",      0x80},
				{NULL}
			};
			modelflags = parsebits(modelflagnames, &line);
		}

		else if (parseanimfield(tok, &line, defaultspec, true))
			;

		else if (!strcasecmp(tok, "mesh"))
		{
			meshoverride &mo = meshoverrides.add();
			mo.name = newstring(mystrtok(&line));
			mo.props = defaultspec.meshprops;

			while(( tok = mystrtok(&line)))
			{
				//fixme: should probably separate this out.
				if (parsemeshfield(tok, &line, mo.props, false))
					;
				else
				{
					printf("unknown mesh token \"%s\"\n", tok);
					break;
				}
			}
		}

		else if (!strcasecmp(tok, "bone"))
		{
			boneoverride &mo = boneoverrides.add();
			mo.name = newstring(mystrtok(&line));
			mo.props = boneoverride::prop();

			while(( tok = mystrtok(&line)))
			{
				//fixme: should probably separate this out.
				if (parsebonefield(tok, &line, mo.props, false))
					;
				else
				{
					printf("unknown mesh token \"%s\"\n", tok);
					break;
				}
			}
		}

		else if (!strcasecmp(tok, "import") || !strcasecmp(tok, "model") || !strcasecmp(tok, "scene") || !strcasecmp(tok, "animation"))
		{
			filespec inspec = defaultspec;

			//first token is always the filename(s)
			inspec.file = newstring(mystrtok(&line));

			while(( tok = mystrtok(&line)))
			{
				if (parseanimfield(tok, &line, inspec, false))
					;
				else
				{
					printf("unknown scene token \"%s\"\n", tok);
					break;
				}
			}

			infiles.add(inspec);
		}
		else if (*tok)
		{
			printf("unsupported command \"%s\"\n", tok);
			continue;
		}

		if ((tok=mystrtok(&line)))
			if (*tok)
				printf("unexpected junk at end-of-line \"%s\" \"%s\"\n", buf, tok);
	}
	delete f;
}

int main(int argc, char **argv)
{
	if(argc <= 1) help(EXIT_FAILURE);

	vector<filespec> infiles;
	vector<hitbox> hitboxes;
	filespec inspec;
	const char *outfile = NULL;

	for(int i = 1; i < argc; i++)
	{
		if(argv[i][0] == '-')
		{
			if(argv[i][1] == '-')
			{
				if(!strcasecmp(&argv[i][2], "cmd")) { if(i + 1 < argc) parsecommands(argv[++i], outfile, infiles, hitboxes); }
				else if(!strcasecmp(&argv[i][2], "noext")) noext = true;
				else if(!strcasecmp(&argv[i][2], "fps")) { if(i + 1 < argc) inspec.fps = atof(argv[++i]); }
				else if(!strcasecmp(&argv[i][2], "name")) { if(i + 1 < argc) inspec.name = argv[++i]; }
				else if(!strcasecmp(&argv[i][2], "loop")) { inspec.flags |= VVM_LOOP; }
				else if(!strcasecmp(&argv[i][2], "start")) { if(i + 1 < argc) inspec.startframe = max(atoi(argv[++i]), 0); }
				else if(!strcasecmp(&argv[i][2], "end")) { if(i + 1 < argc) inspec.endframe = atoi(argv[++i]); }
				else if(!strcasecmp(&argv[i][2], "scale")) { if(i + 1 < argc) inspec.scale = clamp(atof(argv[++i]), 1e-8, 1e8); }
				else if(!strcasecmp(&argv[i][2], "help")) help();
				else if(!strcasecmp(&argv[i][2], "forcejoints")) forcejoints = true;
				else if(!strcasecmp(&argv[i][2], "meshtrans"))
				{
					if(i + 1 < argc) switch(sscanf(argv[++i], "%lf , %lf , %lf", &gmeshtrans.x, &gmeshtrans.y, &gmeshtrans.z))
						{
						case 1: gmeshtrans = Vec3(0, 0, gmeshtrans.x); break;
						}
				}
			}
			else switch(argv[i][1])
				{
				case 'h':
					help();
					break;
				case 's':
					if(i + 1 < argc) gscale = clamp(atof(argv[++i]), 1e-8, 1e8);
					break;
				case 'j':
					forcejoints = true;
					break;
				case 'v':
					verbose = true;
					break;
				case 'q':
					quiet = true;
					break;
				case 'n':
					noext = true;
					break;
				}
		}
		else
		{
			const char *type = strrchr(argv[i], '.');
			if (type && (!strcasecmp(type, ".cmd")||!strcasecmp(type, ".cfg")||!strcasecmp(type, ".txt")||!strcasecmp(type, ".qc")))        //.qc to humour halflife fanboys
				parsecommands(argv[i], outfile, infiles, hitboxes);
			else if(!outfile)
				outfile = argv[i];
			else
			{
				infiles.add(inspec).file = argv[i];
				inspec.reset();
			}
		}
	}

	if(!outfile) fatal("no output file specified");
	else if(infiles.empty()) fatal("no input files specified");

	if(gscale != 1) printf("scale: %f\n", escale);
	if(gmeshtrans != Vec3(0, 0, 0)) printf("mesh translate: %f, %f, %f\n", gmeshtrans.x, gmeshtrans.y, gmeshtrans.z);

	loopv(infiles)
	{
		const filespec &inspec = infiles[i];
		const char *infile = inspec.file, *type = strrchr(infile, '.');
		if (verbose)
			conoutf("importing %s", infile);
		if(!type) fatal("no file type: %s", infile);
		else if(!strcasecmp(type, ".md5mesh"))
		{
			if(!loadmd5mesh(infile, inspec)) fatal("failed reading: %s", infile);
		}
		else if(!strcasecmp(type, ".md5anim"))
		{
			if(!loadmd5anim(infile, inspec)) fatal("failed reading: %s", infile);
		}
		else if(!strcasecmp(type, ".iqe"))
		{
			if(!loadiqe(infile, inspec)) fatal("failed reading: %s", infile);
		}
		else if(!strcasecmp(type, ".smd"))
		{
			if(!loadsmd(infile, inspec)) fatal("failed reading: %s", infile);
		}
		else if(!strcasecmp(type, ".fbx"))
		{
			if(!loadfbx(infile, inspec)) fatal("failed reading: %s", infile);
		}
		else if(!strcasecmp(type, ".obj"))
		{
			if(!loadobj(infile, inspec)) fatal("failed reading: %s", infile);
		}
		else fatal("unknown file type: %s", type);
	}

	genhitboxes(hitboxes);

	loopv(boneoverrides) if (!boneoverrides[i].used)
		conoutf("warning: bone \"%s\" overriden, but not present", boneoverrides[i].name);

	if (noext && meshoverrides.length())
		conoutf("warning: mesh overrides used, but vvm extensions disabled");
	else loopv(meshoverrides)
		conoutf("warning: mesh \"%s\" overriden, but not present", meshoverrides[i].name);


	calcanimdata();

	conoutf("bone list:");
	printbones();
//	printbonelist();

	if (!quiet)
		conoutf("");

	if(writevvm(outfile))
	{
		if (!quiet)
			conoutf("exported %s format file: %s", ext_detected ? "VVM" : "IQM", outfile);
	}
	else fatal("failed writing: %s", outfile);

	return EXIT_SUCCESS;
}

