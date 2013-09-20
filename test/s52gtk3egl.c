// S52gtk3egl.c: simple S52 driver using EGL & GTK3.
//
// SD 2013AUG30 - Vitaly

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2013 Sylvain Duclos sduclos@users.sourceforge.net

    OpENCview is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpENCview is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public License
    along with OpENCview.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "S52.h"

#ifdef S52_USE_AIS
#include "s52ais.h"       // s52ais_*()
#endif

#define  PATH "/home/sduclos/dev/gis/data"
#define  PLIB "PLAUX_00.DAI"
#define  COLS "plib_COLS-3.4.1.rle"
#define  LOGI(...)   g_print(__VA_ARGS__)
#define  LOGE(...)   g_print(__VA_ARGS__)


// test - St-Laurent Ice Route
static S52ObjectHandle _waypnt1 = NULL;
static S52ObjectHandle _waypnt2 = NULL;
static S52ObjectHandle _waypnt3 = NULL;
static S52ObjectHandle _waypnt4 = NULL;

static S52ObjectHandle _leglin1 = NULL;
static S52ObjectHandle _leglin2 = NULL;
static S52ObjectHandle _leglin3 = NULL;

// test - VRMEBL
// S52 object name:"ebline"
//static int             _drawVRMEBLtxt = FALSE;
static S52ObjectHandle _vrmeblA       = NULL;

// test - cursor DISP 9 (instead of IHO PLib DISP 8)
// need to load PLAUX
// S52 object name:"ebline"
static S52ObjectHandle _cursor2 = NULL;

// test - centroid
static S52ObjectHandle _prdare = NULL;

// FIXME: mutex this share data
typedef struct s52droid_state_t {
    // GLib stuff
    GMainLoop *main_loop;
    guint      s52_draw_sigID;
    gpointer   gobject;
    gulong     handler;

    int        do_S52init;

    // initial view
    double     cLat, cLon, rNM, north;     // center of screen (lat,long), range of view(NM)
} s52droid_state_t;

//
typedef struct s52engine {
	GtkWidget       *window;

    // EGL - android or X11 window
    EGLNativeWindowType eglWindow; //GdkDrawable in GTK2 and GdkWindow in GTK3
    EGLDisplay          eglDisplay;
    EGLSurface          eglSurface;
    EGLContext          eglContext;

    // local
    int                 do_S52draw;        // TRUE to call S52_draw()
    int                 do_S52drawLast;    // TRUE to call S52_drawLast() - S52_draw() was called at least once
    int                 do_S52setViewPort; // set in Android callback

    int32_t             width;
    int32_t             height;
    // Xoom - dpi = 160 (density)
    int32_t             dpi;            // = AConfiguration_getDensity(engine->app->config);
    int32_t             wmm;
    int32_t             hmm;

    GTimeVal            timeLastDraw;

    s52droid_state_t    state;
} s52engine;

static s52engine _engine;

//----------------------------------------------
//
// Common stuff
//

#define VESSELTURN_UNDEFINED 129

//------ FAKE AIS - DEBUG ----
// debug - no real AIS, then fake target
#ifdef S52_USE_FAKE_AIS
static S52ObjectHandle _vessel_ais        = NULL;
#define VESSELLABEL "~~MV Non Such~~ "           // last char will be trimmed
// test - ownshp
static S52ObjectHandle _ownshp            = NULL;
#define OWNSHPLABEL "OWNSHP\\n220 deg / 6.0 kt"


#ifdef S52_USE_AFGLOW
#define MAX_AFGLOW_PT (12 * 20)   // 12 min @ 1 vessel pos per 5 sec
//#define MAX_AFGLOW_PT 10        // debug
static S52ObjectHandle _vessel_ais_afglow = NULL;

#endif

#endif
//-----------------------------


static int      _egl_init       (s52engine *engine)
{
    LOGI("s52egl:_egl_init(): starting ..\n");

    if ((NULL!=engine->eglDisplay) && (EGL_NO_DISPLAY!=engine->eglDisplay)) {
        LOGE("_egl_init(): EGL is already up .. skipped!\n");
        return FALSE;
    }

// EGL Error code -
// #define EGL_SUCCESS             0x3000
// #define EGL_NOT_INITIALIZED     0x3001
// #define EGL_BAD_ACCESS          0x3002
// #define EGL_BAD_ALLOC           0x3003
// #define EGL_BAD_ATTRIBUTE       0x3004
// #define EGL_BAD_CONFIG          0x3005
// #define EGL_BAD_CONTEXT         0x3006
// #define EGL_BAD_CURRENT_SURFACE 0x3007
// #define EGL_BAD_DISPLAY         0x3008
// #define EGL_BAD_MATCH           0x3009
// #define EGL_BAD_NATIVE_PIXMAP   0x300A
// #define EGL_BAD_NATIVE_WINDOW   0x300B
// #define EGL_BAD_PARAMETER       0x300C
// #define EGL_BAD_SURFACE         0x300D
// #define EGL_CONTEXT_LOST        0x300E



    EGLDisplay eglDisplay;
    EGLSurface eglSurface;
    EGLContext eglContext;

    // Here specify the attributes of the desired configuration.
    // Below, we select an EGLConfig with at least 8 bits per color
    // component compatible with on-screen windows

    const EGLint eglConfigList[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,

        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,

        EGL_NONE
    };

    EGLBoolean ret = eglBindAPI(EGL_OPENGL_ES_API);
    if (EGL_TRUE != ret)
        LOGE("eglBindAPI() failed. [0x%x]\n", eglGetError());

    eglDisplay = eglGetDisplay(GDK_WINDOW_XDISPLAY(gtk_widget_get_window(engine->window)));
    if (EGL_NO_DISPLAY == eglDisplay)
        LOGE("eglGetDisplay() failed. [0x%x]\n", eglGetError());

    EGLint major = 2;
    EGLint minor = 0;
    if (EGL_FALSE == eglInitialize(eglDisplay, &major, &minor) || EGL_SUCCESS != eglGetError())
        LOGE("eglInitialize() failed. [0x%x]\n", eglGetError());

    LOGI("EGL Version   :%s\n", eglQueryString(eglDisplay, EGL_VERSION));
    LOGI("EGL Vendor    :%s\n", eglQueryString(eglDisplay, EGL_VENDOR));
    LOGI("EGL Extensions:%s\n", eglQueryString(eglDisplay, EGL_EXTENSIONS));

    // Here, the application chooses the configuration it desires. In this
    // sample, we have a very simplified selection process, where we pick
    // the first EGLConfig that matches our criteria
    //EGLint     tmp;
    //EGLConfig  eglConfig[320];
    EGLint     eglNumConfigs = 0;
    EGLConfig  eglConfig;

    //eglGetConfigs(eglDisplay, eglConfig, 320, &tmp);
    eglGetConfigs(eglDisplay, NULL, 0, &eglNumConfigs);
    printf("eglNumConfigs = %i\n", eglNumConfigs);

    /*
    int i = 0;
    for (i = 0; i<eglNumConfigs; ++i) {
        EGLint samples = 0;
        //if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLES, &samples))
        //    printf("eglGetConfigAttrib in loop for an EGL_SAMPLES fail at i = %i\n", i);
        if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig[i], EGL_SAMPLE_BUFFERS, &samples))
            printf("eglGetConfigAttrib in loop for an  EGL_SAMPLE_BUFFERS fail at i = %i\n", i);

        if (samples > 0)
            printf("sample found: %i\n", samples);

    }
    eglGetConfigs(eglDisplay, configs, num_config[0], num_config))
    */

    if (EGL_FALSE == eglChooseConfig(eglDisplay, eglConfigList, &eglConfig, 1, &eglNumConfigs))
        LOGE("eglChooseConfig() failed. [0x%x]\n", eglGetError());
    if (0 == eglNumConfigs)
        LOGE("eglChooseConfig() eglNumConfigs no matching config [0x%x]\n", eglGetError());

    // EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
    // guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
    // As soon as we picked a EGLConfig, we can safely reconfigure the
    // ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
    EGLint vid;
    if (EGL_FALSE == eglGetConfigAttrib(eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &vid))
        LOGE("Error: eglGetConfigAttrib() failed\n");

	engine->eglWindow = (EGLNativeWindowType) gtk_widget_get_window(engine->window);

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, GDK_WINDOW_XID( gtk_widget_get_window(engine->window)), NULL);
    if (EGL_NO_SURFACE == eglSurface || EGL_SUCCESS != eglGetError())
        LOGE("eglCreateWindowSurface() failed. EGL_NO_SURFACE [0x%x]\n", eglGetError());

    // Then we can create the context and set it current:
    EGLint eglContextList[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, eglContextList);
    if (EGL_NO_CONTEXT == eglContext || EGL_SUCCESS != eglGetError())
        LOGE("eglCreateContext() failed. [0x%x]\n", eglGetError());

    if (EGL_FALSE == eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext))
        LOGE("Unable to eglMakeCurrent()\n");

    engine->eglDisplay = eglDisplay;
    engine->eglContext = eglContext;
    engine->eglSurface = eglSurface;

    LOGI("s52egl:_egl_init(): end ..\n");

    return 1;
}

static void     _egl_done       (s52engine *engine)
// Tear down the EGL context currently associated with the display.
{
    if (engine->eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (engine->eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->eglDisplay, engine->eglContext);
        }
        if (engine->eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(engine->eglDisplay, engine->eglSurface);
        }
        eglTerminate(engine->eglDisplay);
    }

    //engine->animating  = 0;
    engine->eglDisplay = EGL_NO_DISPLAY;
    engine->eglContext = EGL_NO_CONTEXT;
    engine->eglSurface = EGL_NO_SURFACE;

    return;
}

static void     _egl_beg        (s52engine *engine)
{
    // On Android, Blit x10 slower whitout
    if (EGL_FALSE == eglWaitGL()) {
        LOGE("_egl_beg(): eglWaitGL() failed. [0x%x]\n", eglGetError());
        return;
    }

    if (EGL_FALSE == eglMakeCurrent(engine->eglDisplay, engine->eglSurface, engine->eglSurface, engine->eglContext)) {
        LOGE("_egl_beg_egl_beg(): eglMakeCurrent() failed. [0x%x]\n", eglGetError());
    }

    return;
}

static void     _egl_end        (s52engine *engine)
{
    if (EGL_TRUE != eglSwapBuffers(engine->eglDisplay, engine->eglSurface)) {
        LOGE("_egl_end(): eglSwapBuffers() failed. [0x%x]\n", eglGetError());
        //return FALSE;
    }

    return;
}

static int      _s52_computeView(s52droid_state_t *state)
{
    double S,W,N,E;

    if (FALSE == S52_getCellExtent(NULL, &S, &W, &N, &E))
        return FALSE;

    state->cLat  =  (N + S) / 2.0;
    state->cLon  =  (E + W) / 2.0;
    state->rNM   = ((N - S) / 2.0) * 60.0;  // FIXME: pick dominan projected N-S or E-W
    state->north = 0.0;

    return TRUE;
}

#ifdef S52_USE_FAKE_AIS
static int      _s52_setupVESSEL(s52droid_state_t *state)
{
    // ARPA
    //_vessel_arpa = S52_newVESSEL(1, dummy, "ARPA label");
    //_vessel_arpa = S52_newVESSEL(1, "ARPA label");
    //S52_pushPosition(_vessel_arpa, _engine.state.cLat + 0.01, _engine.state.cLon - 0.02, 045.0);
    //S52_setVector(_vessel_arpa, 2, 060.0, 3.0);   // water

    // AIS active
    _vessel_ais = S52_newVESSEL(2, NULL);
    S52_setDimension(_vessel_ais, 100.0, 100.0, 15.0, 15.0);
    //S52_pushPosition(_vessel_ais, _engine.state.cLat - 0.02, _engine.state.cLon + 0.02, 045.0);
    //S52_pushPosition(_vessel_ais, state->cLat - 0.04, state->cLon + 0.04, 045.0);
    S52_pushPosition(_vessel_ais, state->cLat - 0.01, state->cLon + 0.01, 045.0);
    S52_setVector(_vessel_ais, 1, 060.0, 16.0);   // ground

    // (re) set label
    S52_setVESSELlabel(_vessel_ais, VESSELLABEL);
    int vesselSelect = 0;  // OFF
    int vestat       = 1;
    int vesselTurn   = VESSELTURN_UNDEFINED;
    S52_setVESSELstate(_vessel_ais, vesselSelect, vestat, vesselTurn);

    // AIS sleeping
    //_vessel_ais = S52_newVESSEL(2, 2, "MV Non Such - sleeping"););
    //S52_pushPosition(_vessel_ais, _engine.state.cLat - 0.02, _engine.state.cLon + 0.02, 045.0);

    // VTS (this will not draw anything!)
    //_vessel_vts = S52_newVESSEL(3, dummy);

#ifdef S52_USE_AFGLOW
    // afterglow
    _vessel_ais_afglow = S52_newMarObj("afgves", S52_LINES, MAX_AFGLOW_PT, NULL, NULL);
#endif

    return TRUE;
}

static int      _s52_setupOWNSHP(s52droid_state_t *state)
{
    _ownshp = S52_newOWNSHP(OWNSHPLABEL);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 0.0, 30.0);
    //_ownshp = S52_setDimension(_ownshp, 150.0, 50.0, 15.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 100.0, 0.0, 15.0);
    //_ownshp = S52_setDimension(_ownshp, 100.0, 0.0, 15.0, 0.0);
    _ownshp = S52_setDimension(_ownshp, 0.0, 100.0, 15.0, 0.0);
    //_ownshp = S52_setDimension(_ownshp, 1000.0, 50.0, 15.0, 15.0);

    S52_pushPosition(_ownshp, state->cLat - 0.02, state->cLon - 0.01, 180.0 + 045.0);

    S52_setVector(_ownshp, 0, 220.0, 6.0);  // ownship use S52_MAR_VECSTB

    return TRUE;
}
#endif  // S52_USE_FAKE_AIS

static int      _s52_setupLEGLIN(void)
{
/*

http://www.marinfo.gc.ca/fr/Glaces/index.asp

SRCN04 CWIS 122100
Bulletin des glaces pour le fleuve et le golfe Saint-Laurent de Les Escoumins aux détroits de
Cabot et de Belle-Isle émis à 2100TUC dimanche 12 février 2012 par le Centre des glaces de
Québec de la Garde côtière canadienne.

Route recommandée no 01
De la station de pilotage de Les Escoumins au
point de changement ALFA:    4820N 06920W au
point de changement BRAVO:   4847N 06830W puis
point de changement CHARLIE: 4900N 06800W puis
point de changement DELTA:   4930N 06630W puis
point de changement ECHO:    4930N 06425W puis
point de changement FOXTROT: 4745N 06000W puis
route normale de navigation.

Route recommandée no 05
Émise à 1431UTC le 17 FEVRIER 2012
par le Centre des Glaces de Québec de la Garde côtière canadienne.

De la station de pilotage de Les Escoumins au
point de changement ALFA:    4820N 06920W au
point de changement BRAVO:   4930N 06630W puis
point de changement CHARLIE: 4945N 06450W puis
point de changement DELTA:   4730N 06000W puis
route normale de navigation.
*/
    typedef struct WPxyz_t {
        double x,y,z;
    } WPxyz_t;

    //*
    WPxyz_t WPxyz[4] = {
        {-69.33333, 48.33333, 0.0},  // WP1 - ALPHA
        {-68.5,     48.78333, 0.0},  // WP2 - BRAVO
        {-68.0,     49.00,    0.0},  // WP3 - CHARLIE
        {-66.5,     49.5,     0.0}   // WP4 - DELTA
    };
    //*/
    /*
    WPxyz_t WPxyz[4] = {
        {-69.33333, 48.33333, 0.0},  // WP1 - ALPHA
        {-66.5,     49.5,     0.0}   // WP2 - BRAVO
    };
    */
    char attVal1[] = "select:2,OBJNAM:ALPHA";    // waypoint on alternate planned route
    char attVal2[] = "select:2,OBJNAM:BRAVO";    // waypoint on alternate planned route
    char attVal3[] = "select:2,OBJNAM:CHARLIE";  // waypoint on alternate planned route
    char attVal4[] = "select:2,OBJNAM:DELTA";    // waypoint on alternate planned route

    _waypnt1 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[0], attVal1);
    _waypnt2 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[1], attVal2);
    _waypnt3 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[2], attVal3);
    _waypnt4 = S52_newMarObj("waypnt", S52_POINT, 1, (double*)&WPxyz[3], attVal4);

#define ALT_RTE 2
    // select: alternate (2) legline for Ice Route 2012-02-12T21:00:00Z
    _leglin1 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[0].y, WPxyz[0].x, WPxyz[1].y, WPxyz[1].x, NULL);
    _leglin2 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[1].y, WPxyz[1].x, WPxyz[2].y, WPxyz[2].x, _leglin1);
    _leglin3 = S52_newLEGLIN(ALT_RTE, 0.0, 0.0, WPxyz[2].y, WPxyz[2].x, WPxyz[3].y, WPxyz[3].x, _leglin2);
    //_leglin4  = S52_newLEGLIN(1, 0.0, 0.0, WPxyz[3].y, WPxyz[3].x, WPxyz[4].y, WPxyz[4].x, _leglin3);

    //_route[0] = _leglin1;
    //_route[1] = _leglin2;
    //_route[2] = _leglin3;
    //_route[3] = _leglin3;
    //S52_setRoute(4, _route);

    /*
    {// test wholin
        char attVal[] = "loctim:1100,usrmrk:test_wholin";
        double xyz[6] = {_engine.state.cLon, _engine.state.cLat, 0.0,  _engine.state.cLon + 0.01, _engine.state.cLat - 0.01, 0.0};
        _wholin = S52_newMarObj("wholin", S52_LINES, 2, xyz, attVal);
    }
    */

    return TRUE;
}

static int      _s52_setupVRMEBL(s52droid_state_t *state)
{
    //char *attVal   = NULL;      // ordinary cursor
    char  attVal[] = "cursty:2,_cursor_label:0.0N 0.0W";  // open cursor
    double xyz[3] = {state->cLon, state->cLat, 0.0};
    int S52_VRMEBL_vrm = TRUE;
    int S52_VRMEBL_ebl = TRUE;
    int S52_VRMEBL_sty = TRUE;  // normalLineStyle
    int S52_VRMEBL_ori = TRUE;  // (user) setOrigin

    _cursor2 = S52_newMarObj("cursor", S52_POINT, 1, xyz, attVal);
    //int ret = S52_toggleObjClassOFF("cursor");
    //LOGE("_s52_setupVRMEBL(): S52_toggleObjClassOFF('cursor'); ret=%i\n", ret);
    //int ret = S52_toggleObjClassON("cursor");
    //LOGE("_s52_setupVRMEBL(): S52_toggleObjClassON('cursor'); ret=%i\n", ret);


    _vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, S52_VRMEBL_ebl, S52_VRMEBL_sty, S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty, !S52_VRMEBL_ori);
    //_vrmeblA = S52_newVRMEBL(S52_VRMEBL_vrm, !S52_VRMEBL_ebl, S52_VRMEBL_sty,  S52_VRMEBL_ori);

    S52_toggleObjClassON("cursor");  // suppression ON
    S52_toggleObjClassON("ebline");
    S52_toggleObjClassON("vrmark");

    return TRUE;
}

static int      _s52_setupPRDARE(s52droid_state_t *state)
// test - centroid (PRDARE: wind farm)
{
    // AREA (CW: to center the text)
    double xyzArea[6*3]  = {
        state->cLon + 0.000, state->cLat + 0.000, 0.0,
        state->cLon - 0.005, state->cLat + 0.004, 0.0,
        state->cLon - 0.010, state->cLat + 0.000, 0.0,
        state->cLon - 0.010, state->cLat + 0.005, 0.0,
        state->cLon + 0.000, state->cLat + 0.005, 0.0,
        state->cLon + 0.000, state->cLat + 0.000, 0.0,
    };

    // PRDARE/WNDFRM51/CATPRA9
    char attVal[] = "CATPRA:9";
    _prdare = S52_newMarObj("PRDARE", S52_AREAS, 6, xyzArea,  attVal);

    return TRUE;
}

static int      _s52_init       (s52engine *engine)
{
    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        LOGE("_init_S52(): no EGL display ..\n");
        return FALSE;
    }

    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

    // return constant value EGL_UNKNOWN (-1) with Mesa
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HORIZONTAL_RESOLUTION, &engine->wmm);
    eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_VERTICAL_RESOLUTION,   &engine->hmm);

    {
		// FIXME: broken on some monitor
		GdkScreen    *screen   = NULL;
		gint         w,h;
		gint         wmm,hmm;

		screen = gdk_screen_get_default();
		w      = gdk_screen_get_width    (screen);
		h      = gdk_screen_get_height   (screen);
		wmm    = gdk_screen_get_width_mm (screen);
		hmm    = gdk_screen_get_height_mm(screen);


        //w   = 1280;
        //h   = 1024;
        //w   = engine->width;
        //h   = engine->height;
        //wmm = 376;
        //hmm = 301; // wrong
        //hmm = 307;

        //LOGE("_init_S52(): start -1- ..\n");

        if (FALSE == S52_init(w, h, wmm, hmm, NULL)) {
            engine->state.do_S52init = FALSE;
            return FALSE;
        }

        //LOGE("_init_S52(): start -2- ..\n");

        S52_setViewPort(0, 0, w, h);

    }

    // can be called any time
    S52_version();

#ifdef S52_USE_EGL
    S52_setEGLcb((EGL_cb)_egl_beg, (EGL_cb)_egl_end, engine);
#endif


    // read cell location fron s52.cfg
    S52_loadCell(NULL, NULL);
    //S52_loadCell("/home/vitaly/CHARTS/for_sasha/GB5X01SE.000", NULL);
	//S52_loadCell("/home/vitaly/CHARTS/for_sasha/GB5X01NE.000", NULL);

    // Ice - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/ice/East_Coast/--0WORLD.shp", NULL);

    // Bathy - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307260_LOD2.merc.tif", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307250_LOD2.merc.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RASTER, 1.0);

    // load AIS select symb.
    //S52_loadPLib("plib-test-priv.rle");


#ifdef S52_USE_WORLD
    // World data
    if (TRUE == S52_loadCell(PATH "/0WORLD/--0WORLD.shp", NULL)) {
        //S52_setMarinerParam(S52_MAR_DISP_WORLD, 0.0);   // default
        S52_setMarinerParam(S52_MAR_DISP_WORLD, 1.0);     // show world
    }
#endif

    // debug - remove clutter from this symb in SELECT mode
    //S52_setS57ObjClassSupp("M_QUAL", TRUE);  // supress display of the U pattern
    //S52_setS57ObjClassSupp("M_QUAL", FALSE);  // displaythe U pattern
    S52_toggleObjClassON ("M_QUAL");           //  suppression ON
    //S52_toggleObjClassOFF("M_QUAL");         //  suppression OFF


    S52_loadPLib(PLIB);
    S52_loadPLib(COLS);

    // -- DEPTH COLOR ------------------------------------
    S52_setMarinerParam(S52_MAR_TWO_SHADES,      0.0);   // 0.0 --> 5 shades
    //S52_setMarinerParam(S52_MAR_TWO_SHADES,      1.0);   // 1.0 --> 2 shades

    // sounding color
    //S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,    10.0);
    S52_setMarinerParam(S52_MAR_SAFETY_DEPTH,    15.0);


    //S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  10.0);
    S52_setMarinerParam(S52_MAR_SAFETY_CONTOUR,  3.0);

    //S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 10.0);
    S52_setMarinerParam(S52_MAR_SHALLOW_CONTOUR, 5.0);

    //S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   11.0);
    S52_setMarinerParam(S52_MAR_DEEP_CONTOUR,   10.0);

    //S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 0.0);  // (default off)
    S52_setMarinerParam(S52_MAR_SHALLOW_PATTERN, 1.0);  // ON
    // -- DEPTH COLOR ------------------------------------



    S52_setMarinerParam(S52_MAR_SHIPS_OUTLINE,   1.0);
    S52_setMarinerParam(S52_MAR_HEADNG_LINE,     1.0);
    S52_setMarinerParam(S52_MAR_BEAM_BRG_NM,     1.0);
    //S52_setMarinerParam(S52_MAR_FULL_SECTORS,    0.0);    // (default ON)

    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_BASE);    // always ON
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_STD);     // default
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_OTHER);
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_BASE | S52_MAR_DISP_CATEGORY_STD | S52_MAR_DISP_CATEGORY_OTHER);
    //S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_STD | S52_MAR_DISP_CATEGORY_OTHER);
    S52_setMarinerParam(S52_MAR_DISP_CATEGORY,   S52_MAR_DISP_CATEGORY_SELECT);

    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_NONE );
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_STD );   // default
    //S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_OTHER);
    S52_setMarinerParam(S52_MAR_DISP_LAYER_LAST, S52_MAR_DISP_LAYER_LAST_SELECT);   // All Mariner (Standard(default) + Other)

    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   0.0);     // DAY (default)
    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   1.0);     // DAY DARK
    S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   5.0);     // DAY 60
    //S52_setMarinerParam(S52_MAR_COLOR_PALETTE,   6.0);     // DUSK 60

    //S52_setMarinerParam(S52_MAR_SCAMIN,          1.0);   // ON (default)
    //S52_setMarinerParam(S52_MAR_SCAMIN,          0.0);   // debug OFF - show all

    // remove QUAPNT01 symbole (black diagonal and a '?')
    S52_setMarinerParam(S52_MAR_QUAPNT01,        0.0);   // off

    S52_setMarinerParam(S52_MAR_DISP_CALIB,      1.0);

    // --- TEXT ----------------------------------------------
    S52_setMarinerParam(S52_MAR_SHOW_TEXT,       1.0);
    //S52_setMarinerParam(S52_MAR_SHOW_TEXT,       0.0);

    S52_setTextDisp(0, 100, TRUE);                      // show all text
    //S52_setTextDisp(0, 100, FALSE);                   // no text

    // cell's legend
    //S52_setMarinerParam(S52_MAR_DISP_LEGEND, 1.0);   // show
    S52_setMarinerParam(S52_MAR_DISP_LEGEND, 0.0);   // hide (default)
    // -------------------------------------------------------


    //S52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 0.0);  // OFF
    //S52_setMarinerParam(S52_MAR_DISP_DRGARE_PATTERN, 1.0);  // ON (default)

    S52_setMarinerParam(S52_MAR_ANTIALIAS,       1.0);   // on
    //S52_setMarinerParam(S52_MAR_ANTIALIAS,       0.0);     // off


    // a delay of 0.0 to tell to not delete old AIS (default +600 sec old)
    //S52_setMarinerParam(S52_MAR_DEL_VESSEL_DELAY, 0.0);

    // debug - use for timing redering
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP);
    //S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX);


    // if first start find where we are looking
    _s52_computeView(&engine->state);
    // then (re)position the 'camera'
    S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    S52_newCSYMB();

    // must be first mariners' object so that the
    // rendering engine place it on top of OWNSHP/VESSEL
    _s52_setupVRMEBL(&engine->state);

    _s52_setupLEGLIN();

    _s52_setupPRDARE(&engine->state);

#ifdef S52_USE_FAKE_AIS
    _s52_setupVESSEL(&engine->state);

    _s52_setupOWNSHP(&engine->state);
#endif


    engine->do_S52draw        = TRUE;
    engine->do_S52drawLast    = TRUE;

    engine->do_S52setViewPort = FALSE;

    engine->state.do_S52init  = FALSE;

    return EGL_TRUE;
}

static int      _s52_done       (s52engine *engine)
{
    (void)engine;

    S52_done();

    return TRUE;
}

#ifdef S52_USE_FAKE_AIS
static int      _s52_updTimeTag (s52engine *engine)
{
    (void)engine;


    // fake one AIS
    if (NULL != _vessel_ais) {
        gchar         str[80];
        GTimeVal      now;
        static double hdg = 0.0;

        hdg = (hdg >= 359.0) ? 0.0 : hdg+1;  // fake rotating hdg

        g_get_current_time(&now);
        g_sprintf(str, "%s %lis", VESSELLABEL, now.tv_sec);
        S52_setVESSELlabel(_vessel_ais, str);
        S52_pushPosition(_vessel_ais, engine->state.cLat - 0.01, engine->state.cLon + 0.01, hdg);
        S52_setVector(_vessel_ais, 1, hdg, 16.0);   // ground

#ifdef S52_USE_AFGLOW
        // stay at the same place but fill internal S52 buffer - in the search for possible leak
        S52_pushPosition(_vessel_ais_afglow, engine->state.cLat, engine->state.cLon, 0.0);
#endif
    }


    return TRUE;
}
#endif

static int      _s52_draw_cb(GtkWidget *widget,
							 GdkEventExpose *event,
							 gpointer user_data)
// return TRUE for the signal to be called again
{
    (void)widget;
    (void)event;

    struct s52engine *engine = (struct s52engine*)user_data;
    //LOGI("s52egl:_s52_draw_cb(): begin .. \n");

    /*
    GTimeVal now;  // 2 glong (at least 32 bits each - but amd64 !?
    g_get_current_time(&now);
    if (0 == (now.tv_sec - engine->timeLastDraw.tv_sec))
        goto exit;
    //*/

    if (NULL == engine) {
        LOGE("_s52_draw_cb(): no engine ..\n");
        goto exit;
    }

    if ((NULL==engine->eglDisplay) || (EGL_NO_DISPLAY==engine->eglDisplay)) {
        LOGE("_s52_draw_cb(): no display ..\n");
        goto exit;
    }

    // wait for libS52 to init - no use to go further - bailout
    if (TRUE == engine->state.do_S52init) {
        LOGI("s52egl:_s52_draw_cb(): re-starting .. waiting for S52_init() to finish\n");
        goto exit;
    }

    // no draw at all, the window is not visible
    if ((FALSE==engine->do_S52draw) && (FALSE==engine->do_S52drawLast)) {
        //LOGI("s52egl:_s52_draw_cb(): nothing to draw (do_S52draw & do_S52drawLast FALSE)\n");
        goto exit;
    }

#ifndef S52_USE_EGL
    _egl_beg(engine);
#endif

    // draw background
    if (TRUE == engine->do_S52draw) {
        if (TRUE == engine->do_S52setViewPort) {
            eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_WIDTH,  &engine->width);
            eglQuerySurface(engine->eglDisplay, engine->eglSurface, EGL_HEIGHT, &engine->height);

            S52_setViewPort(0, 0, engine->width, engine->height);

            engine->do_S52setViewPort = FALSE;
        }

        S52_draw();
        engine->do_S52draw = FALSE;
    }

    // draw AIS
    if (TRUE == engine->do_S52drawLast) {

#ifdef S52_USE_FAKE_AIS
        _s52_updTimeTag(engine);
#endif
        S52_drawLast();
    }

#ifndef S52_USE_EGL
    _egl_end(engine);
#endif


exit:

    // debug
    //LOGI("s52egl:_s52_draw_cb(): end .. \n");

    return EGL_TRUE;
}

//static int      _s52_screenShot(void)
// debug - S57 obj ID of Becancour Cell (CA579016.000)
//{
//    static int takeScreenShot = TRUE;
//    if (TRUE == takeScreenShot) {
//        //S52_dumpS57IDPixels("test.png", 954, 200, 200); // waypnt
//        S52_dumpS57IDPixels("test.png", 556, 200, 200); // land
//    }
//    takeScreenShot = FALSE;
//
//    return EGL_TRUE;
//}



//----------------------------------------------
//
// X11 specific code
// for testing EGL / GLES2 outside of android
//
/*


static int      _X11_handleXevent(gpointer user_data)
{
    s52engine *engine = (s52engine *) user_data;

    while (XPending(engine->dpy)) {
        XEvent event;
        XNextEvent(engine->dpy, &event);

        switch (event.type) {
        case ConfigureNotify:
            engine->width  = event.xconfigure.width;
            engine->height = event.xconfigure.height;
            S52_setViewPort(0, 0, event.xconfigure.width, event.xconfigure.height);

            break;

        case Expose:
            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;
            g_signal_emit(G_OBJECT(engine->state.gobject), engine->state.s52_draw_sigID, 0);
            break;

        case ButtonRelease:
            {
                XButtonReleasedEvent *mouseEvent = (XButtonReleasedEvent *)&event;

                const char *name = S52_pickAt(mouseEvent->x, engine->height - mouseEvent->y);
                if (NULL != name) {
                    unsigned int S57ID = atoi(name+7);
                    g_print("OBJ(%i, %i): %s\n", mouseEvent->x, engine->height - mouseEvent->y, name);
                    g_print("AttList=%s\n", S52_getAttList(S57ID));

                    {   // debug:  S52_xy2LL() --> S52_LL2xy() should be the same
                        // NOTE:  LL (0,0) is the OpenGL origine (not GTK origine)
                        double Xlon = 0.0;
                        double Ylat = 0.0;
                        S52_xy2LL(&Xlon, &Ylat);
                        S52_LL2xy(&Xlon, &Ylat);
                        g_print("DEBUG: xy2LL(0,0) --> LL2xy ==> Xlon: %f, Ylat: %f\n", Xlon, Ylat);
                    }

                    if (0 == g_strcmp0("vessel", name)) {
                        g_print("vessel found\n");
                        unsigned int S57ID = atoi(name+7);

                        S52ObjectHandle vessel = S52_getMarObjH(S57ID);
                        if (NULL != vessel) {
                            S52_setVESSELstate(vessel, 1, 0, VESSELTURN_UNDEFINED);
                            //g_print("AttList: %s\n", S52_getAttList(S57ID));
                        }
                    }
                }

            }
            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;
            g_signal_emit(G_OBJECT(engine->state.gobject), engine->state.s52_draw_sigID, 0);
            break;

        case KeyPress:
        case KeyRelease: {
            // /usr/include/X11/keysymdef.h
            unsigned int keycode = ((XKeyEvent *)&event)->keycode;
            unsigned int keysym  = XkbKeycodeToKeysym(engine->dpy, keycode, 0, 1);

            // ESC
            if (XK_Escape == keysym) {
                g_main_loop_quit(engine->state.main_loop);
                return TRUE;
            }
            // Load Cell
            if (XK_F1 == keysym) {
                S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA279037.000", NULL);
                //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA379035.000", NULL);
                //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000", NULL);
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // Done Cell
            if (XK_F2 == keysym) {
                S52_doneCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA279037.000");
                //S52_doneCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000");
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // Disp. Cat. SELECT
            if (XK_F3 == keysym) {
                S52_setMarinerParam(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT);
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // VRMEBL toggle
            if (XK_F4 == keysym) {
                _drawVRMEBLtxt = !_drawVRMEBLtxt;
                if (TRUE == _drawVRMEBLtxt) {
                    S52_toggleObjClassOFF("cursor");
                    S52_toggleObjClassOFF("ebline");
                    S52_toggleObjClassOFF("vrmark");

                    {
                        double brg = 0.0;
                        double rge = 0.0;
                        S52_setVRMEBL(_vrmeblA, 100, 100, &brg, &rge);
                        S52_setVRMEBL(_vrmeblA, 100, 500, &brg, &rge);
                    }

                    S52_pushPosition(_cursor2, engine->state.cLat, engine->state.cLon, 0.0);

                } else {
                    S52_toggleObjClassON("cursor");
                    S52_toggleObjClassON("ebline");
                    S52_toggleObjClassON("vrmark");
                }

                return TRUE;
            }
            // Rot. Buoy Light
            if (XK_F5 == keysym) {
                S52_setMarinerParam(S52_MAR_ROT_BUOY_LIGHT, 180.0);
                engine->do_S52draw = TRUE;
                return TRUE;
            }
            // ENC list
            if (XK_F6 == keysym) {
                g_print("%s\n", S52_getCellNameList());
                return TRUE;
            }
            // load AIS select symb.
            if (XK_F7 == keysym) {
                S52_loadPLib("plib-test-priv.rle");
                return TRUE;
            }
            // debug - unicode at S57ID:552 on CA579041.000 - Rimouski
            if (XK_F8 == keysym) {
                const char *str = S52_getAttList(552);
                g_print("s52eglx:F8:%s\n", str);

                return TRUE;
            }


            // debug
            g_print("s52egl.c:keysym: %i\n", keysym);


            //
            // debug - basic view movement
            //

            double delta = (engine->state.rNM / 10.0) / 60.0;

            // Move left, left arrow
            if (XK_Left      == keysym) {
                engine->state.cLon -= delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move up, up arrow
            if (XK_Up        == keysym) {
                engine->state.cLat += delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move right, right arrow
            if (XK_Right     == keysym) {
                engine->state.cLon += delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // Move down, down arrow
            if (XK_Down      == keysym) {
                engine->state.cLat -= delta;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // zoom in
            if (XK_Page_Up   == keysym) {
                engine->state.rNM -= (engine->state.rNM / 10.0);
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // zoom out
            if (XK_Page_Down == keysym) {
                engine->state.rNM += (engine->state.rNM / 10.0);
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // rot -10.0 deg
            if (XK_Home      == keysym) {
#ifdef S52_USE_EGL
                S52_drawBlit(0.0, 0.0, 0.0, -10.0);
#else
                _egl_beg(engine);
                S52_drawBlit(0.0, 0.0, 0.0, -10.0);
                _egl_end(engine);
#endif
                engine->state.north -= 10.0;
                if (0.0 > engine->state.north)
                    engine->state.north += 360.0;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }
            // rot +10.0 deg
            if (XK_End       == keysym) {
#ifdef S52_USE_EGL
                S52_drawBlit(0.0, 0.0, 0.0, +10.0);
#else
                _egl_beg(engine);
                S52_drawBlit(0.0, 0.0, 0.0, +10.0);
                _egl_end(engine);
#endif


                engine->state.north += 10.0;  // +10.0 deg
                if (360.0 <= engine->state.north)
                    engine->state.north -= 360.0;
                S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);
            }

            engine->do_S52draw     = TRUE;
            engine->do_S52drawLast = TRUE;
        }
        break;

        }  // switch
    }      // while

    return TRUE;
}
*/

static gboolean _scroll(GdkEventKey *event)
{
    switch(event->keyval) {
        case GDK_KEY_Left : _engine.state.cLon -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_KEY_Right: _engine.state.cLon += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_KEY_Up   : _engine.state.cLat += _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        case GDK_KEY_Down : _engine.state.cLat -= _engine.state.rNM/(60.0*10.0); S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }

    return TRUE;
}

static gboolean _zoom(GdkEventKey *event)
{
    switch(event->keyval) {
        // zoom in
    	case GDK_KEY_Page_Up  : _engine.state.rNM /= 2.0; S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
        // zoom out
        case GDK_KEY_Page_Down: _engine.state.rNM *= 2.0; S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north); break;
    }

    return TRUE;
}

static gboolean _rotation(GdkEventKey *event)
{
    //S52_getView(&_view);

    switch(event->keyval) {
        // -
        case GDK_KEY_minus:
            _engine.state.north += 1.0;
            if (360.0 < _engine.state.north)
                _engine.state.north -= 360.0;
            break;
        // +
        case GDK_KEY_equal:
        case GDK_KEY_plus :
            _engine.state.north -= 1.0;
            if (_engine.state.north < 0.0)
                _engine.state.north += 360.0;
            break;
    }

    //S52_setView(&_view);
    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);

    return TRUE;
}

static gboolean _toggle(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName);
    val = S52_setMarinerParam(paramName, !val);

    return TRUE;
}

static gboolean _meterInc(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName);
    val = S52_setMarinerParam(paramName, ++val);

    return TRUE;
}

static gboolean _meterDec(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName);
    val = S52_setMarinerParam(paramName, --val);

    return TRUE;
}

static gboolean _disp(S52MarinerParameter paramName, const char disp)
{
    double val = (double) disp;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _cpal(S52MarinerParameter paramName, double val)
{
    val = S52_getMarinerParam(paramName) + val;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _inc(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName) + 15.0;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _mmInc(S52MarinerParameter paramName)
{
    double val = 0.0;

    val = S52_getMarinerParam(paramName) + 0.01;

    val = S52_setMarinerParam(paramName, val);

    return TRUE;
}

static gboolean _dumpParam()
{
    g_print("S52_MAR_SHOW_TEXT         t %4.1f\n", S52_getMarinerParam(S52_MAR_SHOW_TEXT));
    g_print("S52_MAR_TWO_SHADES        w %4.1f\n", S52_getMarinerParam(S52_MAR_TWO_SHADES));
    g_print("S52_MAR_SAFETY_CONTOUR    c %4.1f\n", S52_getMarinerParam(S52_MAR_SAFETY_CONTOUR));
    g_print("S52_MAR_SAFETY_DEPTH      d %4.1f\n", S52_getMarinerParam(S52_MAR_SAFETY_DEPTH));
    g_print("S52_MAR_SHALLOW_CONTOUR   a %4.1f\n", S52_getMarinerParam(S52_MAR_SHALLOW_CONTOUR));
    g_print("S52_MAR_DEEP_CONTOUR      e %4.1f\n", S52_getMarinerParam(S52_MAR_DEEP_CONTOUR));
    g_print("S52_MAR_SHALLOW_PATTERN   s %4.1f\n", S52_getMarinerParam(S52_MAR_SHALLOW_PATTERN));
    g_print("S52_MAR_SHIPS_OUTLINE     o %4.1f\n", S52_getMarinerParam(S52_MAR_SHIPS_OUTLINE));
    g_print("S52_MAR_DISTANCE_TAGS     f %4.1f\n", S52_getMarinerParam(S52_MAR_DISTANCE_TAGS));
    g_print("S52_MAR_TIME_TAGS         g %4.1f\n", S52_getMarinerParam(S52_MAR_TIME_TAGS));
    g_print("S52_MAR_BEAM_BRG_NM       y %4.1f\n", S52_getMarinerParam(S52_MAR_BEAM_BRG_NM));
    g_print("S52_MAR_FULL_SECTORS      l %4.1f\n", S52_getMarinerParam(S52_MAR_FULL_SECTORS));
    g_print("S52_MAR_SYMBOLIZED_BND    b %4.1f\n", S52_getMarinerParam(S52_MAR_SYMBOLIZED_BND));
    g_print("S52_MAR_SYMPLIFIED_PNT    p %4.1f\n", S52_getMarinerParam(S52_MAR_SYMPLIFIED_PNT));
    g_print("S52_MAR_DISP_CATEGORY   7-0 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_CATEGORY));
    g_print("S52_MAR_COLOR_PALETTE     k %4.1f\n", S52_getMarinerParam(S52_MAR_COLOR_PALETTE));
    g_print("S52_MAR_FONT_SOUNDG       n %4.1f\n", S52_getMarinerParam(S52_MAR_FONT_SOUNDG));
    g_print("S52_MAR_DATUM_OFFSET      m %4.1f\n", S52_getMarinerParam(S52_MAR_DATUM_OFFSET));
    g_print("S52_MAR_SCAMIN            u %4.1f\n", S52_getMarinerParam(S52_MAR_SCAMIN));
    g_print("S52_MAR_ANTIALIAS         i %4.1f\n", S52_getMarinerParam(S52_MAR_ANTIALIAS));
    g_print("S52_MAR_QUAPNT01          j %4.1f\n", S52_getMarinerParam(S52_MAR_QUAPNT01));
    g_print("S52_MAR_DISP_OVERLAP      z %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_OVERLAP));
    g_print("S52_MAR_DISP_LAYER_LAST   1 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_LAYER_LAST));
    g_print("S52_MAR_ROT_BUOY_LIGHT    2 %4.1f\n", S52_getMarinerParam(S52_MAR_ROT_BUOY_LIGHT));
    g_print("S52_MAR_DISP_CRSR_POS     3 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_CRSR_POS));
    g_print("S52_MAR_DISP_GRATICULE    4 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_GRATICULE));
    g_print("S52_MAR_HEADNG_LINE       5 %4.1f\n", S52_getMarinerParam(S52_MAR_HEADNG_LINE));
    g_print("S52_MAR_DISP_WHOLIN       6 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_WHOLIN));
    g_print("S52_MAR_DISP_LEGEND       3 %4.1f\n", S52_getMarinerParam(S52_MAR_DISP_LEGEND));
    g_print("S52_CMD_WRD_FILTER     F1-5 %4.1f\n", S52_getMarinerParam(S52_CMD_WRD_FILTER));
    g_print("S52_MAR_DOTPITCH_MM_X    F7 %4.2f\n", S52_getMarinerParam(S52_MAR_DOTPITCH_MM_X));
    g_print("S52_MAR_DOTPITCH_MM_Y    F8 %4.2f\n", S52_getMarinerParam(S52_MAR_DOTPITCH_MM_Y));
    g_print("S52_MAR_DISP_NODATA_LAYER F9 %4.2f\n", S52_getMarinerParam(S52_MAR_DISP_NODATA_LAYER));

    int crntVal = (int) S52_getMarinerParam(S52_CMD_WRD_FILTER);

    g_print("\tFilter State:\n");
    g_print("\tF1 - S52_CMD_WRD_FILTER_SY: %s\n", (S52_CMD_WRD_FILTER_SY & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF2 - S52_CMD_WRD_FILTER_LS: %s\n", (S52_CMD_WRD_FILTER_LS & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF3 - S52_CMD_WRD_FILTER_LC: %s\n", (S52_CMD_WRD_FILTER_LC & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF4 - S52_CMD_WRD_FILTER_AC: %s\n", (S52_CMD_WRD_FILTER_AC & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF5 - S52_CMD_WRD_FILTER_AP: %s\n", (S52_CMD_WRD_FILTER_AP & crntVal) ? "TRUE" : "FALSE");
    g_print("\tF6 - S52_CMD_WRD_FILTER_TX: %s\n", (S52_CMD_WRD_FILTER_TX & crntVal) ? "TRUE" : "FALSE");

    return TRUE;
}
static gboolean configure_event(GtkWidget         *widget,
                                GdkEventConfigure *event,
                                gpointer           data)
{
    (void)event;
    (void)data;


    // FIXME: find the new screen size
    GtkAllocation allocation;
    gtk_widget_get_allocation(GTK_WIDGET(widget), &allocation);

    //gtk_widget_get_allocation(GTK_WIDGET(_engine.window), &allocation);
    //gtk_widget_size_allocate(GTK_WIDGET(_engine.window), &allocation);

    //GtkRequisition requisition;
    //gtk_widget_get_child_requisition(widget, &requisition);

    _engine.width  = allocation.width;
    _engine.height = allocation.height;

    _s52_computeView(&_engine.state);
    S52_setView(_engine.state.cLat, _engine.state.cLon, _engine.state.rNM, _engine.state.north);
    S52_setViewPort(0, 0, allocation.width, allocation.height);

    _engine.do_S52draw = TRUE;

    return TRUE;
}

static gboolean key_release_event(GtkWidget   *widget,
                                  GdkEventKey *event,
                                  gpointer     data)
{
    (void)widget;
    (void)data;

    switch(event->keyval) {
        case GDK_KEY_Left  :
        case GDK_KEY_Right :
        case GDK_KEY_Up    :
        case GDK_KEY_Down  :_scroll(event);            break;

        case GDK_KEY_equal :
        case GDK_KEY_plus  :
        case GDK_KEY_minus :_rotation(event);          break;

        case GDK_KEY_Page_Down:
        case GDK_KEY_Page_Up:_zoom(event);             break;


     //   case GDK_KEY_Escape:_resetView(&_engine.state);                break;
        case GDK_KEY_r     : /*gtk_widget_draw(widget, NULL);*/break;
     //   case GDK_KEY_h     :_doRenderHelp = !_doRenderHelp;
     //                   _usage("s52gtk2");
     //                   break;
        case GDK_KEY_v     :g_print("%s\n", S52_version());    break;
        case GDK_KEY_x     :_dumpParam();                      break;
        case GDK_KEY_q     :gtk_main_quit();                   break;

        case GDK_KEY_w     :_toggle(S52_MAR_TWO_SHADES);       break;
        case GDK_KEY_s     :_toggle(S52_MAR_SHALLOW_PATTERN);  break;
        case GDK_KEY_o     :_toggle(S52_MAR_SHIPS_OUTLINE);    break;
        case GDK_KEY_l     :_toggle(S52_MAR_FULL_SECTORS);     break;
        case GDK_KEY_b     :_toggle(S52_MAR_SYMBOLIZED_BND);   break;
        case GDK_KEY_p     :_toggle(S52_MAR_SYMPLIFIED_PNT);   break;
        case GDK_KEY_n     :_toggle(S52_MAR_FONT_SOUNDG);      break;
        case GDK_KEY_u     :_toggle(S52_MAR_SCAMIN);           break;
        case GDK_KEY_i     :_toggle(S52_MAR_ANTIALIAS);        break;
        case GDK_KEY_j     :_toggle(S52_MAR_QUAPNT01);         break;
        case GDK_KEY_z     :_toggle(S52_MAR_DISP_OVERLAP);     break;
        //case GDK_1     :_toggle(S52_MAR_DISP_LAYER_LAST);  break;
        case GDK_KEY_1     :_meterInc(S52_MAR_DISP_LAYER_LAST);break;
        case GDK_KEY_exclam:_meterDec(S52_MAR_DISP_LAYER_LAST);break;

        case GDK_KEY_2     :_inc(S52_MAR_ROT_BUOY_LIGHT);      break;

        case GDK_KEY_3     :_toggle(S52_MAR_DISP_CRSR_POS);
                            _toggle(S52_MAR_DISP_LEGEND);
                            _toggle(S52_MAR_DISP_CALIB);
                            _toggle(S52_MAR_DISP_DRGARE_PATTERN);
                            break;

        case GDK_KEY_4     :_toggle(S52_MAR_DISP_GRATICULE);   break;
        case GDK_KEY_5     :_toggle(S52_MAR_HEADNG_LINE);      break;

        //case GDK_t     :_meterInc(S52_MAR_SHOW_TEXT);      break;
        //case GDK_T     :_meterDec(S52_MAR_SHOW_TEXT);      break;
        case GDK_KEY_t     :
        case GDK_KEY_T     :_toggle  (S52_MAR_SHOW_TEXT);      break;
        case GDK_KEY_c     :_meterInc(S52_MAR_SAFETY_CONTOUR); break;
        case GDK_KEY_C     :_meterDec(S52_MAR_SAFETY_CONTOUR); break;
        case GDK_KEY_d     :_meterInc(S52_MAR_SAFETY_DEPTH);   break;
        case GDK_KEY_D     :_meterDec(S52_MAR_SAFETY_DEPTH);   break;
        case GDK_KEY_a     :_meterInc(S52_MAR_SHALLOW_CONTOUR);break;
        case GDK_KEY_A     :_meterDec(S52_MAR_SHALLOW_CONTOUR);break;
        case GDK_KEY_e     :_meterInc(S52_MAR_DEEP_CONTOUR);   break;
        case GDK_KEY_E     :_meterDec(S52_MAR_DEEP_CONTOUR);   break;
        case GDK_KEY_f     :_meterInc(S52_MAR_DISTANCE_TAGS);  break;
        case GDK_KEY_F     :_meterDec(S52_MAR_DISTANCE_TAGS);  break;
        case GDK_KEY_g     :_meterInc(S52_MAR_TIME_TAGS);      break;
        case GDK_KEY_G     :_meterDec(S52_MAR_TIME_TAGS);      break;
        case GDK_KEY_y     :_meterInc(S52_MAR_BEAM_BRG_NM);    break;
        case GDK_KEY_Y     :_meterDec(S52_MAR_BEAM_BRG_NM);    break;
        case GDK_KEY_m     :_meterInc(S52_MAR_DATUM_OFFSET);   break;
        case GDK_KEY_M     :_meterDec(S52_MAR_DATUM_OFFSET);   break;

        //case GDK_KEY_7     :_disp(S52_MAR_DISP_CATEGORY, 'D'); break; // DISPLAYBASE
        //case GDK_KEY_8     :_disp(S52_MAR_DISP_CATEGORY, 'S'); break; // STANDARD
        //case GDK_KEY_9     :_disp(S52_MAR_DISP_CATEGORY, 'O'); break; // OTHER
        //case GDK_KEY_0     :_disp(S52_MAR_DISP_CATEGORY, 'A'); break; // OTHER (all)
        //case GDK_KEY_7     :_disp(S52_MAR_DISP_CATEGORY, 0);   break; // DISPLAYBASE
        //case GDK_KEY_8     :_disp(S52_MAR_DISP_CATEGORY, 1);   break; // STANDARD
        //case GDK_KEY_9     :_disp(S52_MAR_DISP_CATEGORY, 2);   break; // OTHER
        //case GDK_KEY_0     :_disp(S52_MAR_DISP_CATEGORY, 3);   break; // OTHER (all)
        case GDK_KEY_7     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_BASE);   break; // DISPLAYBASE
        case GDK_KEY_8     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_STD);    break; // STANDARD
        case GDK_KEY_9     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_OTHER);  break; // OTHER
        case GDK_KEY_0     :_disp(S52_MAR_DISP_CATEGORY, S52_MAR_DISP_CATEGORY_SELECT);  break; // OTHER (all)

        case GDK_KEY_k     :_cpal(S52_MAR_COLOR_PALETTE,  1.0);break;
        case GDK_KEY_K     :_cpal(S52_MAR_COLOR_PALETTE, -1.0);break;

        case GDK_KEY_6     :_meterInc(S52_MAR_DISP_WHOLIN);    break;
        case GDK_KEY_asciicircum:
        case GDK_KEY_question:
        case GDK_KEY_caret :_meterDec(S52_MAR_DISP_WHOLIN);    break;


        //case GDK_3     :_cpal("S52_MAR_COLOR_PALETTE", 2.0); break; // DAY_WHITEBACK
        //case GDK_4     :_cpal("S52_MAR_COLOR_PALETTE", 3.0); break; // DUSK
        //case GDK_5     :_cpal("S52_MAR_COLOR_PALETTE", 4.0); break; // NIGHT

        //case GDK_KEY_F1    :S52_doneCell("/home/vitaly/CHARTS/for_sasha/GB5X01SE.000"); break;
        //case GDK_KEY_F2    :S52_doneCell("/home/vitaly/CHARTS/for_sasha/GB5X01NE.000"); break;
        case GDK_KEY_F1    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_SY); break;
        case GDK_KEY_F2    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LS); break;
        case GDK_KEY_F3    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_LC); break;
        case GDK_KEY_F4    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AC); break;
        case GDK_KEY_F5    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_AP); break;
        case GDK_KEY_F6    :S52_setMarinerParam(S52_CMD_WRD_FILTER, S52_CMD_WRD_FILTER_TX); break;

        case GDK_KEY_F7    :_mmInc(S52_MAR_DOTPITCH_MM_X); break;
        case GDK_KEY_F8    :_mmInc(S52_MAR_DOTPITCH_MM_Y); break;

        case GDK_KEY_F9    :_toggle(S52_MAR_DISP_NODATA_LAYER); break;

        default:
            g_print("key: 0x%04x\n", event->keyval);
    }

    // redraw
    _engine.do_S52draw = TRUE;

    return TRUE;
}

static gboolean step (gpointer data)
{
    gdk_window_invalidate_rect(GDK_WINDOW(data), NULL, TRUE);

    return TRUE;
}

int main (int argc, char** argv)
{
    gtk_init (&argc, &argv);

    _engine.window = GTK_WIDGET(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_default_size(GTK_WINDOW(_engine.window), 500, 500);
    gtk_window_set_title(GTK_WINDOW(_engine.window), "OpenGL ES2 in GTK3 application");

    gtk_widget_show_all(_engine.window);

    _egl_init(&_engine);
    _s52_init(&_engine);

    gtk_widget_set_app_paintable     (_engine.window, TRUE );
    gtk_widget_set_double_buffered   (_engine.window, FALSE);
    gtk_widget_set_redraw_on_allocate(_engine.window, TRUE );

    g_signal_connect(G_OBJECT(_engine.window), "destroy",           G_CALLBACK(gtk_main_quit),     NULL);
    g_signal_connect(G_OBJECT(_engine.window), "draw",              G_CALLBACK(_s52_draw_cb),     &_engine);
    g_signal_connect(G_OBJECT(_engine.window), "key_release_event", G_CALLBACK(key_release_event), NULL);
    g_signal_connect(G_OBJECT(_engine.window), "configure_event",   G_CALLBACK(configure_event),   NULL);

    g_timeout_add(500, step, gtk_widget_get_window(_engine.window)); // 0.5 sec

    gtk_main();

    _s52_done(&_engine);
    _egl_done(&_engine);

    g_print("%s .. done\n", argv[0]);

    return 0;
}