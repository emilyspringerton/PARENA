/* tests/netkit/vncd.c -- a tiny REAL VNC server built on libvncserver (the TightVNC lineage), used only by
 * tests/netkit/test_rfb_interop.py to check nk_rfb.prn against an implementation this repo did not write.
 *
 *   vncd <port> <password|-> <out.rgb>
 *
 * Paints a 320x200 pattern, waits for a client, then on a timer: draws a rectangle, does a CopyRect, rings the
 * bell, sends cut text, and resizes the desktop to 256x160 (DesktopSize). When the client disconnects (or after
 * 12 s) it writes its final framebuffer as raw RGB to <out.rgb> and exits. Test-only; never shipped.
 */
#include <rfb/rfb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static int W = 320, H = 200;
static rfbScreenInfoPtr S;
static unsigned char *fbuf;

static void put(int x, int y, int r, int g, int b) {
    rfbPixelFormat *f = &S->serverFormat;
    unsigned v = ((unsigned)r << f->redShift) | ((unsigned)g << f->greenShift) | ((unsigned)b << f->blueShift);
    memcpy(fbuf + ((size_t)y * W + x) * 4, &v, 4);
}
static void get(int x, int y, int *r, int *g, int *b) {
    rfbPixelFormat *f = &S->serverFormat; unsigned v; memcpy(&v, fbuf + ((size_t)y * W + x) * 4, 4);
    *r = (v >> f->redShift) & 255; *g = (v >> f->greenShift) & 255; *b = (v >> f->blueShift) & 255;
}
static void pattern(void) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int band = (x / 40 + y / 25) % 3;
        put(x, y, band == 0 ? 200 : x & 255, band == 1 ? 180 : y & 255, (x * y) & 255);
    }
}
static void fillrect(int x0, int y0, int w, int h, int r, int g, int b) {
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) put(x, y, r, g, b);
    rfbMarkRectAsModified(S, x0, y0, x0 + w, y0 + h);
}
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }
static void dump(const char *path) {
    FILE *o = fopen(path, "wb"); if (!o) return;
    fprintf(o, "%d %d\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int r, g, b; get(x, y, &r, &g, &b); fputc(r, o); fputc(g, o); fputc(b, o); }
    fclose(o);
}

int main(int argc, char **argv) {
    int port = atoi(argv[1]); const char *pw = argv[2]; const char *outp = argv[3];
    int ac = 1; char *av[] = { argv[0], NULL };
    S = rfbGetScreen(&ac, av, W, H, 8, 3, 4);
    fbuf = malloc((size_t)W * H * 4); S->frameBuffer = (char *)fbuf;
    S->port = port; S->ipv6port = port + 1; S->alwaysShared = TRUE; S->desktopName = "vncd-interop";
    static char *pwlist[2];
    if (strcmp(pw, "-") != 0) { pwlist[0] = (char *)pw; pwlist[1] = NULL; S->authPasswdData = pwlist; S->passwordCheck = rfbCheckPasswordByList; }
    /* A fully transparent 1x1 cursor: otherwise the server paints its pointer into the frames it sends to clients that
       did not ask for cursor shapes (it did: 13 pixels at 0,0), which would make the byte-for-byte check compare the
       cursor. (rfbSetCursor(NULL) segfaults in libvncserver 0.9.14.) */
    rfbSetCursor(S, rfbMakeXCursor(1, 1, (char *)"\0", (char *)"\0"));
    S->cursorX = -64; S->cursorY = -64;   /* park the pointer off-screen */
    pattern();
    rfbInitServer(S);
    fprintf(stderr, "vncd listening on %d\n", port); fflush(stderr);
    double t0 = 0, start = now(); int step = 0; int seen = 0;
    while (rfbIsActive(S) && now() - start < 20) {
        rfbProcessEvents(S, 20000);
        if (S->clientHead) { if (!seen) { seen = 1; t0 = now(); } } else if (seen) break;
        if (!seen) continue;
        double dt = now() - t0;
        if (step == 0 && dt > 0.6) { fillrect(30, 20, 90, 60, 250, 10, 10); step = 1; }
        else if (step == 1 && dt > 1.0) { fillrect(200, 100, 17, 19, 10, 250, 120); step = 2; }
        else if (step == 2 && dt > 1.4) {   /* CopyRect: move the red box (a real overlapping scroll) */
            for (int y = 79; y >= 20; y--) for (int x = 30; x < 120; x++) { int r, g, b; get(x, y, &r, &g, &b); put(x + 25, y + 15, r, g, b); }
            rfbDoCopyRect(S, 55, 35, 145, 95, 25, 15);
            step = 3; }
        else if (step == 3 && dt > 1.8) { rfbSendBell(S); rfbSendServerCutText(S, "hello from vncd", 15); step = 4; }
        else if (step == 4 && dt > 2.2) {   /* DesktopSize */
            int nw = 256, nh = 160; unsigned char *nb = malloc((size_t)nw * nh * 4);
            unsigned char *old = fbuf; W = nw; H = nh; fbuf = nb; rfbNewFramebuffer(S, (char *)fbuf, nw, nh, 8, 3, 4);
            free(old); pattern(); rfbMarkRectAsModified(S, 0, 0, nw, nh); step = 5; }
        else if (step == 5 && dt > 2.8) { fillrect(10, 10, 40, 40, 0, 0, 255); step = 6; }
        else if (step == 6 && dt > 3.5) break;
    }
    dump(outp);
    rfbShutdownServer(S, TRUE);
    return 0;
}
