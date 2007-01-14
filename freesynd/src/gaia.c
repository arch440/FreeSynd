#include "SDL.h"
#include "lua.h"
#include "lauxlib.h"

#include "narcissus.h" /* REV8, CreateZSurface */
#include "munin.h"
#include "hugin.h"

extern struct SDL_Surface *screen_surface;
static const Uint8 off[6][2] = { 00,00, 00,16, 00,32, 32,00, 32,16, 32,32, };

static struct SDL_Surface *subtiles;
static Uint16 subtile_table[1536];

static void blit_tile(Sint16 x, Sint16 y, Sint16 t)
{
  struct SDL_Rect S = { 0, 0, 32, 16 };
  struct SDL_Rect T = { 0, 0, 32, 16 };
  Uint8 i;

  if(x < -64 || y < -48 || x > width || y > height) return;

  for(i = 0; i < 6; i++) {
    T.x = x + off[i][0];
    T.y = y + off[i][1];
    S.y = subtile_table[t*6+i]*16;
    SDL_BlitSurface(subtiles, &S, screen_surface, &T);
  }
}

static int load_subtiles(struct SDL_RWops *F, Uint8 *z, Uint16 c)
{ /* reads count=c subtiles into a 32x(16*c) bit paletted surface */
  struct { Uint32 t; Uint32 p[5]; } __attribute__((packed)) planar;
  Uint8 x,y,p, *q = (void *) &planar;

  while(c--) {                                         /* each subtile       */
    for(y=0; y < 16; y++) {            	               /* each row           */
      SDL_RWread(F, &planar, 20, 1);   		       /* read subtile       */
      for(p=0; p < 20; p++)            	               /* each byte          */
	q[p] = REV8(q[p]);                             /* reverse the bits   */
      for(p=0; p < 4; p++) {           	               /* each plane         */
	for(x=0; x < 32; x++) {        	               /* each pixel         */
	  if(!(planar.t    & 1 << x)) {                /* if not transparent */
	    if(planar.p[p] & 1 << x)                   /* if bitplane is set */
	      z[y*32 + x] |= 1 << p;                   /* increment color    */
      } } }
      for(x=0; x < 32; x++)             	       /* each pixel         */
        if(planar.t & 1 << x)                          /* if transparent     */
	  z[y*32 + x]  = 255;          		       /* ... */
    }
    z += 32*16;                        		       /* paint next subtile */
  }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define MAP 32

struct Map {
  Uint32 xs, ys, zs;
  union {
    Uint32 l[0];
    Uint8 m[0];
  };
};

static struct Map *M;

Uint8 *tile_ref(struct Map *M, Uint16 x, Uint16 y, Uint16 z)
{
  x /= MAP; y /= MAP; z /= MAP;
  if(x >= M->xs || y >= M->ys || z >= M->zs) return NULL;
  return &M->m[ M->l[ y*M->xs + x ] ] + z;
}

Uint8 *raw_ref(struct Map *M, Uint16 x, Uint16 y, Uint16 z)
{
  if(x >= M->xs || y >= M->ys || z >= M->zs) return NULL;
  return &M->m[ M->l[ y*M->xs + x ] ] + z;
}

void load_map(struct SDL_RWops *F)
{
  Uint32 s, x, y, z;

  s = SDL_RWseek(F, 0, SEEK_END), SDL_RWseek(F, 0, SEEK_SET);

  M = malloc(s);
  SDL_RWread(F, M, s, 1);

  printf("  +  %d x %d x %d map loaded\n", M->xs, M->ys, M->zs, s);

  {             /* Verify tile_refs */
    Uint8 *t;
    z = M->zs-1;

    for(x = 0; x < M->xs; x++)
      for(y = 0; y < M->ys; y++)
	if((t = raw_ref(M, x, y, z)) == NULL)
	  printf("%d,%d,%d\tno reference\n", x, y, z);
	else
	  if( ((void*)t - (void*)M) > s)
	    printf("%d,%d,%d\treferences beyond array"
		   " %p %p %d\n",
		   x, y, z, M, t, t - M->m);
  }
}

int map_new(struct lua_State *L)
{
  struct rwops *rw;
  struct Map *M;
  Uint32 s, x, y, z;

  luaL_argcheck(L, is_rw(L, 1), 1, "Expected an rw");
  rw = lua_touserdata(L, 1);

  s = SDL_RWseek(rw->F, 0, SEEK_END), SDL_RWseek(rw->F, 0, SEEK_SET);

  if((M = lua_newuserdata(L, s)) != NULL) {
    if(SDL_RWread(rw->F, M, s, 1)) {
      printf("  +  %d x %d x %d map loaded\n", M->xs, M->ys, M->zs, s);

      Uint8 *t;   /* Verify tile_refs */
      z = M->zs-1;

      for(x = 0; x < M->xs; x++)
	for(y = 0; y < M->ys; y++)
	  if((t = tile_ref(M, x*MAP,y*MAP,z*MAP))==NULL)
	    return printf("%d,%d,%d\thas no reference!\n", x, y, z) * 0;
	  else
	    if( ((void*)t - (void*) M) > s)
	      return printf("%d,%d,%d\treferences beyond array"
			    " %p %p %d\n",
			    x, y, z,
			    M, t, t - M->m) * 0;
      return 1;
    }
  }
  return 0;
}

/************************************************************************\

  Construction of the map-to-screen tranformation matrix.
  =======================================================

  First, most obviously, we need to rotate 45 degrees.
      +-                 -+     +-  __        __     -+
      |   cos(t)  sin(t)  |     |  V 2 / 2  -V 2 / 2  |
  A = |                   |  =  |   __        __      |
      |  -sin(t)  cos(t)  |     |  V 2 / 2   V 2 / 2  |
      +-                 -+     +-                   -+

  Second, we have to squash to half height. (Actually, it could be
  interpreted as x-rotation by acos(2)= 60 degrees, but that'd take a 3x3)

      +-             .           -+            +-              -+
      |  1    0           0       |            |   1       0    |
  B = |              |            |[1-2,1-2] = |                |
      |  0   cos(60)   -+sin(60)  |            |   0     1 / 2  |
      |  -  -  -  -  +            |            +-              -+
      |  0  -sin(60)     cos(60)  |
      +-                         -+

  For 320x200 we want 5x6.5 map tile diagonals on the screen, or 5*sqrt(2)
  by 6.5*sqrt(2). Multiplying by (5, 5) gets us (10*sqrt(2), 0) instead of
  (320, 0), and (6.5, 6.5)->(0, 12.5*sqrt(2)/4) instead of (0,200).)
  So, if we wanted to normalize to 1,1 we'd scale by the inverses of
  those, but that's not quite what we want.

      +--                         --+
      |    320                      |   +-                     -+
      |  ---------        0         |   | 64/sqrt(2)     0      |
  C = |  5*sqrt(2)                  | = |                       |
      |                  200        |   |    0       64/sqrt(2) |
      |     0         --------      |   +-                     -+
      |              12.5*sqrt(2)/4 |
      +--                         --+

  Putting it all toghether we get
                  +-      -+
                  | 32 -32 |
  T = C * B * A = |        |
                  | 16  16 |
                  +-      -+

  Yuck. Ok, that was the hard way.

  Ok, scaled so we get 32 units per tile in map-space we get just
  MS(x,y) = x   -  y, x/2 + y/2. Thankfully, the inverse is also trivial:
  SM(x,y) = x/2 + 2y, -2x + 2y.

  Also, I've lied a bit. Since map to screen translation is an R^3 to R^2
  and mapping z is trivial, I've omitted it, the result is this:
      +-          -+
      | 32 -32   0 |
  T = |            |
      | 16  16 -16 |
      +-          -+

  Scaling back so we get better positioning in map-land:
      +-            -+
      |   1  -1   0  |
  T = |              |
      | 1/2 1/2 -1/2 |
      +-            -+

-- t = -45 * pi / 180
-- A=[ cos(t) sin(t); -sin(t) cos(t) ]
-- B=[ 1 0 ; 0 1/2 ]
-- C=[ 2/sqrt(2) 0 ; 0 2/sqrt(2) ]

\************************************************************************/

#define MS_x(x,y,z) ( ( 2*(x) - 2*(y) + 0*(z) ) / 2)
#define MS_y(x,y,z) ( ( 1*(x) + 1*(y) - 1*(z) ) / 2)
#define SM_x(x,y)   ( ( 1*(x) + 2*(y) ) / 2 )
#define SM_y(x,y)   ( (-1*(x) + 2*(y) ) / 2 )
#define TILE(x) ((x) & ~(MAP-1))
#define SUB(x)  ((x) % MAP)

static Sint16 LX, LY; /* LAST DRAWN HERE */

#define W 64
#define H 32

extern struct lua_State *L;

#define GLOM(x,y,z) ((z << 16) + (y << 8) + (x << 0))

/* TODO: Move to AVATAR
 */
static Uint8 do_sprites(Uint32 k, Uint16 sx, Uint16 sy)
{
  Sint16 i = 0, x, y, a, f;

  if(lua_gettop(L) && lua_istable(L, -1)) {
    lua_pushnumber(L, k); //GLOM(mx, my, mz));
    lua_gettable(L, -2);

    if(lua_istable(L, -1)) {
      foreach(L, -1) {
	if(!lua_istable(L, -1)) { printf("non-mo in cell-list\n"); continue; }

	x = get_int(L, -1, "x", 0);
	y = get_int(L, -1, "y", 0);
	a = get_int(L, -1, "a", 1);
	f = get_int(L, -1, "f", 0);
	f = (blit_frame(sx + MS_x(SUB(x), SUB(y), 0),
			sy + MS_y(SUB(x), SUB(y), 0), a, f))? f = 0: f+1;
	lua_pushnumber(L, f);
	lua_setfield(L, -2, "f");
      }
      i = 1;
    } else i = 0;
    lua_pop(L, 1);
  }
  if(lua_gettop(L) != 1) debug_lua_stack(L, "do_sprite");
  return i;
}

void blit_map(/*struct Map *M, */ Sint16 wx, Sint16 wy)
{
  Sint16 sx=0, sy=0;
  Uint8  *w = NULL;

  wx -= SM_x(width/2, height/2);
  wy -= SM_y(width/2, height/2);

  LX=wx+MAP; LY=wy; /* SAVE THESE. (+wtf?)*/

  sx = -MS_x(SUB(wx), SUB(wy), 0)-W;  /* sub-tile scroll */
  sy = -MS_y(SUB(wx), SUB(wy), 0);    /* adjustment      */

  wx /= MAP; wy /= MAP;

  Uint32 q = ~0;

  if(lua_gettop(L)) debug_lua_stack(L, "bmap>");
  lua_getfield(L, LUA_GLOBALSINDEX, "sprites");

  Sint8 x=0, y=0, z=0;
  Sint16 xl, yl, zf;
  xl = SM_x(width,        0) / MAP + 2;
  yl = SM_y(    0, 2*height) / MAP + M->zs + 3;

  for   ( y = 0 ; y <   yl ; y++) {    /* far to near   */
  for   ( z = 0 ; z < M->zs; z++) {    /* bottom to top */
  for   ( x = 0 ; x <   xl ; x++) {    /* left to right */
    Uint16 mx, my, qx, qy;
    mx = wx+x+(y/2)+(y&1);
    my = wy-x+(y/2);

    w = raw_ref(M, mx, my, z);

    qx = sx+64*x+(32*(y&1));
    qy = sy+16*y-16*z;

    if(w != NULL && *w > 5)
      blit_tile(qx, qy, *w);

    do_sprites(GLOM(mx, my, z), qx+32, qy+16);
    

  } } } /* for for for */
  lua_pop(L, 1);
  if(lua_gettop(L)) debug_lua_stack(L, "bmap<");
}

/*
  #define SZMx(x,y)   ( ( 1*(x) + 2*(y) ) / 2 )
  #define SZMy(x,y)   ( (-1*(x) + 2*(y) ) / 2 )
*/
int point_at(struct lua_State *L) /* Harder than it seemed. */
{
  Sint16 x, y, z, mx, my;
  Uint8 *p = NULL;

  luaL_argcheck(L, lua_isnumber(L, 1), 1, "Expected x coordinate");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "Expected y coordinate");
  x = lua_tonumber(L, 1);
  y = lua_tonumber(L, 2);

  /* Ok, now we shift x,y and ask for the  z=0 intercept. */
  for(z = M->zs; --z >= 0 && (!p || *p < 5);) {
    mx = SM_x(x, y + z*H/2)+LX;
    my = SM_y(x, y + z*H/2)+LY;
    p = tile_ref(M, mx, my, z*MAP);
    /*printf("z: %2d mx: %d my:%d mz:%3d : %d\n",
      z, mx, my, z*MAP, (p)? *p: -1);*/
  }
  if(p == NULL) {
    lua_pushnumber(L, -1);
    lua_pushnumber(L, -1);
    lua_pushnumber(L, -1);
    lua_pushnumber(L, -1);
  } else {
    lua_pushnumber(L, mx);
    lua_pushnumber(L, my);
    lua_pushnumber(L,  z);
    lua_pushnumber(L, *p);
  }
  return 4;
}

int tile_at(struct lua_State *L)
{
  Sint32 x, y, z;
  Uint8 *p;

  luaL_argcheck(L, lua_isnumber(L, 1), 1, "Expected x coordinate");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "Expected y coordinate");
  x = lua_tonumber(L, 1);
  y = lua_tonumber(L, 2);
  z = (lua_isnumber(L, 3))?
      lua_tonumber(L, 3): 0;

  if((p = tile_ref(M, x, y, z)) != NULL)
    lua_pushnumber(L, *p);
  else
    lua_pushnumber(L, -1);
  return 1;
}

int scr_to_map(struct lua_State *L)
{
  double x, y;

  luaL_argcheck(L, lua_isnumber(L, 1), 1, "Expected number");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "Expected number");
  x = lua_tonumber(L, 1);
  y = lua_tonumber(L, 2);
  lua_pushnumber(L, (double) SM_x(x, y)+LX);
  lua_pushnumber(L, (double) SM_y(x, y)+LY);
  return 2;
}

int map_to_scr(struct lua_State *L)
{
  double x, y, z;

  luaL_argcheck(L, lua_isnumber(L, 1), 1, "Expected number");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "Expected number");
  x = lua_tonumber(L, 1);
  y = lua_tonumber(L, 2);
  z = lua_isnumber(L, 3)? lua_tonumber(L, 3): 0;
  lua_pushnumber(L, (double) MS_x(x+LX, y+LY, z));
  lua_pushnumber(L, (double) MS_y(x+LX, y+LY, z));
  return 2;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

extern struct lua_State *L;

int init_gaia(void)
{
  struct SDL_Surface *Z;
  struct SDL_RWops *F;
  Sint32 w;
  int i, x=0;

  
  if( (F = SDL_RWFromFile("data/hblk01.dat","r")) == NULL)
    abend(0);

  for(i=0; i < 256*6; i++) {
    w = SDL_ReadLE32(F);
    w = (w < 6144)? 0: (w - 6144) / 320;
    subtile_table[i] = w;
    x = (w > x)? w : x;
  }
  printf("Gaia tile module:\n  +  %d unique subtiles.\n", x);
  x++;
  
  if(!(Z = CreateZSurface(32,16*x)) )
    SDL_RWclose(F), abend(0);

  load_subtiles(F, Z->pixels, x);
  if(1) subtiles = Z;
  else {
    if( (subtiles = SDL_CreateRGBSurface(SDL_HWSURFACE,
					 32,16*x,32,
					 RR,GG,BB,AA)) )
      SDL_BlitSurface(Z, NULL, subtiles, NULL);
    SDL_FreeSurface(Z);
  }

  SDL_RWclose(F);

  F = SDL_RWFromFile("maps/map05.dat","r");
  load_map(F);

  lua_pushnumber(L, MAP);
  lua_setfield(L, LUA_GLOBALSINDEX, "scale");
  lua_register(L, "scr_to_map", scr_to_map);
  lua_register(L, "map_to_scr", map_to_scr);
  lua_register(L, "tile_at", tile_at);
  lua_register(L, "point_at", point_at);
  return subtiles != NULL;
}
