// Campiello old-school demo (see OldSchoolDemo.h). A pirate galleon on a moonlit copper sea, a
// bouncing raster logo, a sine scroller, and an original Caribbean-flavoured chiptune.

#include "OldSchoolDemo.h"

#include <Font.h>
#include <MediaDefs.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <SoundPlayer.h>
#include <View.h>
#include <Window.h>

#include <cmath>
#include <cstring>

static const uint32 kMsgDemoTick = 'dtck'; // animation frame tick

// ---------------------------------------------------------------------------- 8-bit sine (in asm)

static int8_t gSinTable[256];
static bool   gSinReady = false;

static void InitSinTable()
{
	if (gSinReady)
		return;
	for (int i = 0; i < 256; ++i)
		gSinTable[i] = (int8_t)(sinf((float)i * 6.2831853f / 256.0f) * 120.0f);
	gSinReady = true;
}

// Sine of an 8-bit angle (0..255 is a full turn), range about -120..120. The wrap-around mask, the
// table index and the sign-extending byte load are done in inline x86-64 assembly.
static inline int AsmSin(unsigned angle)
{
	int result;
	unsigned a = angle;
	const int8_t* table = gSinTable;
	__asm__(
		"andl $0xFF, %[a]\n\t"
		"movsbl (%[tbl],%q[a],1), %[res]\n\t"
		: [res] "=&r"(result), [a] "+r"(a)
		: [tbl] "r"(table)
		: "cc");
	return result;
}

static uint8 ClampByte(int v) { return (uint8)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// ---------------------------------------------------------------------------------- the demo view

class DemoView : public BView {
public:
	explicit DemoView(BRect frame)
		: BView(frame, "demo", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS)
	{
		InitSinTable();
		SetViewColor(B_TRANSPARENT_COLOR); // we paint every pixel each frame
	}
	~DemoView() override { delete fRunner; }

	void AttachedToWindow() override
	{
		MakeFocus(true);
		BMessage tick(kMsgDemoTick);
		fRunner = new BMessageRunner(BMessenger(this), &tick, 40000); // ~25 fps
	}

	void MessageReceived(BMessage* m) override
	{
		if (m->what == kMsgDemoTick) { ++fPhase; Invalidate(); return; }
		BView::MessageReceived(m);
	}

	void MouseDown(BPoint) override { Window()->PostMessage(B_QUIT_REQUESTED); }
	void KeyDown(const char* bytes, int32 n) override
	{
		if (n > 0 && bytes[0] == B_ESCAPE) Window()->PostMessage(B_QUIT_REQUESTED);
	}

	// Translucent fill helper (source-over with the colour's alpha).
	void AlphaColor(uint8 r, uint8 g, uint8 b, uint8 a)
	{
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		SetHighColor(r, g, b, a);
	}
	void SolidMode() { SetDrawingMode(B_OP_COPY); }

	void Draw(BRect) override
	{
		const float w = Bounds().Width();
		const float h = Bounds().Height();
		const unsigned p = fPhase;
		const float seaTop = h * 0.63f;

		// --- Sky: a deep night-to-sunset gradient banded with copper stripes ---
		for (int y = 0; y < (int)seaTop; ++y) {
			float t = (float)y / seaTop;
			int copper = AsmSin((unsigned)(y * 4 + p * 2)) / 6;
			SetHighColor(ClampByte((int)(18 + 210 * t) + copper),
				ClampByte((int)(14 + 120 * t) + copper / 2),
				ClampByte((int)(46 + 40 * t) + copper), 255);
			StrokeLine(BPoint(0, y), BPoint(w, y));
		}

		DrawStarfield(w, seaTop, p);
		DrawMoon(w * 0.23f, seaTop * 0.44f, 30.0f, p);

		// --- Seagulls drifting and flapping ---
		for (int i = 0; i < 3; ++i) {
			float gx = (float)(((int)(p * 1 + i * 220)) % (int)(w + 40)) - 20;
			float gy = seaTop * (0.20f + 0.10f * i) + AsmSin((unsigned)(p * 2 + i * 60)) / 14.0f;
			DrawSeagull(gx, gy, (unsigned)(p * 6 + i * 30));
		}

		// --- Sea: layered waves, a depth gradient, and foam crests ---
		for (int x = 0; x < (int)w; ++x) {
			float t = (float)x / w;
			float wave = AsmSin((unsigned)(x * 2 + p * 4)) / 10.0f
				+ AsmSin((unsigned)(x * 5 - p * 3)) / 18.0f;
			SetHighColor((uint8)(8 + 16 * t), (uint8)(46 + 54 * t), (uint8)(96 + 66 * t), 255);
			StrokeLine(BPoint(x, seaTop + wave), BPoint(x, h));
			float mid = seaTop + 26 + AsmSin((unsigned)(x * 3 - p * 5)) / 12.0f;
			SetHighColor((uint8)(20 + 20 * t), (uint8)(70 + 60 * t), (uint8)(130 + 60 * t), 255);
			StrokeLine(BPoint(x, mid), BPoint(x, mid + 1));
			if (AsmSin((unsigned)(x * 7 + p * 6)) > 96) {
				SetHighColor(220, 235, 245);
				FillRect(BRect(x, seaTop + wave, x, seaTop + wave));
			}
		}

		DrawWaterGlint(w * 0.23f, seaTop, h, p);

		// --- The pirate galleon (with its reflection), sailing right to left ---
		float span = w + 260;
		float shipX = w + 110 - (float)((p * 2) % (unsigned)span);
		float deck = seaTop - 4 + AsmSin(p * 3) / 22.0f;
		DrawReflection(shipX, deck, seaTop, p);
		DrawShip(shipX, deck, p);

		// --- Bouncing raster logo up top ---
		DrawTitleLogo("CAMPIELLO", w, 46.0f, p);

		// --- Copper bars + sine scroller along the bottom ---
		DrawCopperBars(w, h - 48, h - 10, p);
		DrawScroller("GREETINGS FROM CAMPIELLO  -  WORLD O NETWORKING LIVES AGAIN ON HAIKU  -  "
			"HELLO TO ALL THE OLD SCHOOL SAILORS OUT THERE  -  HOIST THE COLOURS!      ", h - 26, p);
	}

private:
	// A twinkling starfield with an occasional shooting star streaking across.
	void DrawStarfield(float w, float seaTop, unsigned p)
	{
		for (int i = 0; i < 80; ++i) {
			float sx = (float)((i * 71 + 17) % (int)w);
			float sy = (float)((i * 37) % (int)(seaTop * 0.72f));
			int tw = AsmSin((unsigned)(p * 5 + i * 24));
			if (tw > 40) {
				SetHighColor(255, 255, ClampByte(200 + tw / 3));
				FillRect(BRect(sx, sy, sx + (tw > 105 ? 1 : 0), sy));
			}
		}
		unsigned sp = p % 500; // a shooting star every ~20 seconds
		if (sp < 34) {
			float t = sp / 34.0f;
			float sxs = w * 0.12f + t * w * 0.55f;
			float sys = seaTop * 0.10f + t * seaTop * 0.28f;
			AlphaColor(255, 255, 236, (uint8)(230 * (1.0f - t)));
			SetPenSize(2.0f);
			StrokeLine(BPoint(sxs, sys), BPoint(sxs - 22, sys - 9));
			SetPenSize(1.0f);
			SolidMode();
		}
	}

	void DrawMoon(float cx, float cy, float r, unsigned p)
	{
		for (int g = 6; g >= 1; --g) { // soft glow halos
			AlphaColor(255, 238, 200, (uint8)(10 + g * 3));
			FillEllipse(BPoint(cx, cy), r + g * 7, r + g * 7);
		}
		SolidMode();
		SetHighColor(250, 244, 214);
		FillEllipse(BPoint(cx, cy), r, r);
		AlphaColor(220, 210, 175, 90); // a few craters
		FillEllipse(BPoint(cx - r * 0.3f, cy - r * 0.2f), r * 0.22f, r * 0.22f);
		FillEllipse(BPoint(cx + r * 0.25f, cy + r * 0.15f), r * 0.16f, r * 0.16f);
		FillEllipse(BPoint(cx + r * 0.05f, cy - r * 0.4f), r * 0.12f, r * 0.12f);
		SolidMode();
		(void)p;
	}

	void DrawSeagull(float cx, float cy, unsigned p)
	{
		float flap = AsmSin(p) / 24.0f; // wings beat with the sine
		SetHighColor(30, 30, 40);
		SetPenSize(2.0f);
		StrokeLine(BPoint(cx - 8, cy + flap), BPoint(cx, cy - 3 - flap));
		StrokeLine(BPoint(cx, cy - 3 - flap), BPoint(cx + 8, cy + flap));
		SetPenSize(1.0f);
	}

	void DrawSail(float cx, float topY, float halfTop, float halfBot, float height, int curl)
	{
		float botY = topY + height;
		BPoint s[5] = {
			BPoint(cx - halfTop, topY), BPoint(cx + halfTop, topY),
			BPoint(cx + halfBot + curl, botY - 4), BPoint(cx + curl * 0.5f, botY + 7),
			BPoint(cx - halfBot + curl, botY - 4) };
		SetHighColor(238, 232, 214);
		FillPolygon(s, 5);
		// a soft shaded belly on the lee side
		AlphaColor(120, 110, 90, 70);
		BPoint sh[3] = { BPoint(cx + halfTop, topY),
			BPoint(cx + halfBot + curl, botY - 4), BPoint(cx + curl * 0.5f, botY + 7) };
		FillPolygon(sh, 3);
		SolidMode();
		SetHighColor(206, 198, 176); // reef bands
		StrokeLine(BPoint(cx - halfTop * 0.8f, topY + height * 0.42f),
			BPoint(cx + halfTop * 0.8f + curl, topY + height * 0.42f));
		StrokeLine(BPoint(cx - halfBot * 0.8f, topY + height * 0.72f),
			BPoint(cx + halfBot * 0.8f + curl, topY + height * 0.72f));
	}

	void DrawShip(float x, float y, unsigned p)
	{
		// --- Hull: a curved sheer with a raised aftcastle (left) and a beakhead (right) ---
		SetHighColor(70, 42, 22);
		BPoint hull[13] = {
			BPoint(x - 84, y - 18), BPoint(x - 58, y - 18), BPoint(x - 54, y - 4),
			BPoint(x - 10, y - 8), BPoint(x + 40, y - 6), BPoint(x + 70, y - 10),
			BPoint(x + 96, y - 20), BPoint(x + 78, y - 2), BPoint(x + 62, y + 24),
			BPoint(x + 30, y + 34), BPoint(x - 34, y + 34), BPoint(x - 60, y + 22),
			BPoint(x - 80, y + 6) };
		FillPolygon(hull, 13);
		// planking trim + a gold rail
		SetHighColor(120, 80, 40);
		StrokeLine(BPoint(x - 78, y + 14), BPoint(x + 66, y + 14));
		SetHighColor(196, 160, 96);
		StrokeLine(BPoint(x - 82, y - 6), BPoint(x + 72, y - 8));
		// gun ports
		SetHighColor(24, 16, 10);
		for (float gx = x - 46; gx <= x + 50; gx += 16)
			FillRect(BRect(gx, y + 3, gx + 7, y + 10));
		// stern lantern, flickering
		int flick = 150 + AsmSin(p * 9) / 4;
		AlphaColor(255, 210, 120, (uint8)ClampByte(flick));
		FillEllipse(BPoint(x - 82, y - 22), 3.5f, 4.5f);
		SolidMode();

		// --- Bowsprit + jib ---
		SetHighColor(60, 38, 18);
		SetPenSize(3.0f);
		StrokeLine(BPoint(x + 74, y - 8), BPoint(x + 120, y - 34));
		SetPenSize(1.0f);
		SetHighColor(232, 226, 208);
		BPoint jib[3] = { BPoint(x + 118, y - 33), BPoint(x + 80, y - 30), BPoint(x + 84, y - 6) };
		FillPolygon(jib, 3);

		// --- Three masts with yards + billowing sails (course + topsail) ---
		const float mastX[3] = { x - 40, x + 2, x + 46 };
		const float mastTop[3] = { y - 108, y - 140, y - 96 };
		int curl = AsmSin(p * 4) / 26;
		for (int m = 0; m < 3; ++m) {
			float mx = mastX[m], top = mastTop[m];
			SetHighColor(54, 34, 18); // mast
			FillRect(BRect(mx - 2, top, mx + 2, y - 2));
			SetHighColor(44, 28, 14); // yards (spars)
			SetPenSize(2.0f);
			StrokeLine(BPoint(mx - 30, top + 20), BPoint(mx + 30, top + 20));
			StrokeLine(BPoint(mx - 34, y - 46), BPoint(mx + 34, y - 46));
			SetPenSize(1.0f);
			DrawSail(mx, top + 22, 22, 30, 40, curl);          // topsail
			DrawSail(mx, y - 44, 30, 36, 40, curl + curl / 2); // course sail
		}

		// --- Rigging: shrouds from each masthead down to the hull, and stays ---
		SetHighColor(30, 22, 14);
		for (int m = 0; m < 3; ++m) {
			float mx = mastX[m], top = mastTop[m];
			for (int s = -1; s <= 1; ++s)
				StrokeLine(BPoint(mx, top), BPoint(mx + s * 30, y - 2));
		}
		StrokeLine(BPoint(x - 40, y - 108), BPoint(x + 2, y - 140));  // fore-main stay
		StrokeLine(BPoint(x + 2, y - 140), BPoint(x + 46, y - 96));   // main-mizzen stay
		StrokeLine(BPoint(x + 2, y - 140), BPoint(x + 120, y - 34));  // main-bowsprit stay

		// --- Crow's nest on the mainmast ---
		SetHighColor(50, 32, 16);
		FillRect(BRect(x - 6, y - 118, x + 10, y - 110));

		// --- The Jolly Roger, rippling on the mainmast top ---
		float ft = y - 156, fx = x + 4;
		SetHighColor(18, 18, 18);
		BPoint flag[8];
		for (int i = 0; i < 4; ++i) {
			float rip = AsmSin((unsigned)(p * 6 + i * 40)) / 10.0f;
			flag[i] = BPoint(fx + i * 9, ft + rip);
			flag[7 - i] = BPoint(fx + i * 9, ft + 15 + rip);
		}
		FillPolygon(flag, 8);
		float sk = fx + 15; // skull centre
		SetHighColor(240, 240, 240);
		FillEllipse(BPoint(sk, ft + 7), 4.2f, 4.6f);            // skull
		FillRect(BRect(sk - 2, ft + 9, sk + 2, ft + 12));       // jaw
		SetHighColor(18, 18, 18);
		FillEllipse(BPoint(sk - 1.6f, ft + 6), 1.1f, 1.3f);     // eyes
		FillEllipse(BPoint(sk + 1.6f, ft + 6), 1.1f, 1.3f);
		SetHighColor(240, 240, 240);
		SetPenSize(1.4f);
		StrokeLine(BPoint(sk - 5, ft + 12), BPoint(sk + 5, ft + 4)); // crossbones
		StrokeLine(BPoint(sk - 5, ft + 4), BPoint(sk + 5, ft + 12));
		SetPenSize(1.0f);

		// --- Bow foam and a long wake trailing behind the hull ---
		for (int i = 0; i < 9; ++i) {
			float fxo = x + 58 + i * 11 + AsmSin((unsigned)(p * 8 + i * 40)) / 20.0f;
			AlphaColor(230, 240, 248, (uint8)ClampByte(165 - i * 17));
			FillEllipse(BPoint(fxo, y + 30 + AsmSin((unsigned)(p * 5 + i * 30)) / 24.0f),
				5.4f - i * 0.4f, 3.2f - i * 0.22f);
		}
		SolidMode();
	}

	// A faded, vertically flipped echo of the ship wavering on the water.
	void DrawReflection(float x, float deck, float waterY, unsigned p)
	{
		AlphaColor(30, 60, 90, 90);
		BPoint hull[6] = {
			BPoint(x - 80, 2 * waterY - (deck + 6)), BPoint(x + 92, 2 * waterY - (deck + 6)),
			BPoint(x + 60, 2 * waterY - (deck + 30)), BPoint(x + 28, 2 * waterY - (deck + 38)),
			BPoint(x - 34, 2 * waterY - (deck + 38)), BPoint(x - 58, 2 * waterY - (deck + 30)) };
		FillPolygon(hull, 6);
		const float mastX[3] = { x - 40, x + 2, x + 46 };
		SetPenSize(2.0f);
		for (int m = 0; m < 3; ++m) {
			float wob = AsmSin((unsigned)(p * 6 + m * 50)) / 14.0f;
			StrokeLine(BPoint(mastX[m] + wob, waterY + 4),
				BPoint(mastX[m] - wob, waterY + 34));
		}
		SetPenSize(1.0f);
		SolidMode();
	}

	// A shimmering column of moonlight on the water, fading with depth.
	void DrawWaterGlint(float cx, float seaTop, float h, unsigned p)
	{
		for (float yy = seaTop + 2; yy < h; yy += 4) {
			float width = 7 + AsmSin((unsigned)((int)yy * 4 + p * 6)) / 9.0f;
			float off = AsmSin((unsigned)((int)yy * 3 + p * 5)) / 11.0f;
			int a = 90 - (int)((yy - seaTop) * 0.55f);
			if (a <= 0)
				continue;
			AlphaColor(255, 242, 205, (uint8)a);
			FillRect(BRect(cx - width + off, yy, cx + width + off, yy + 2));
		}
		SolidMode();
	}

	// A big bold title whose letters bounce on the sine and cycle through warm hues.
	void DrawTitleLogo(const char* text, float w, float baseY, unsigned p)
	{
		BFont font(be_bold_font);
		font.SetSize(34);
		SetFont(&font);
		int len = (int)strlen(text);
		const float adv = 30.0f;
		float startX = (w - len * adv) * 0.5f + 4.0f;
		for (int i = 0; i < len; ++i) {
			float cx = startX + i * adv;
			float bob = AsmSin((unsigned)(p * 4 + i * 22)) / 6.0f;
			char c[2] = { text[i], 0 };
			AlphaColor(0, 0, 0, 150); // drop shadow
			DrawString(c, BPoint(cx + 3, baseY + bob + 3));
			SolidMode();
			int hue = AsmSin((unsigned)(p * 3 + i * 18));
			SetHighColor(ClampByte(210 + hue / 4), ClampByte(150 + hue / 2),
				ClampByte(70 + hue / 2), 255);
			DrawString(c, BPoint(cx, baseY + bob));
		}
		SetFont(be_plain_font);
	}

	// Classic translucent copper bars cycling behind the scroller band.
	void DrawCopperBars(float w, float y0, float y1, unsigned p)
	{
		for (int yy = (int)y0; yy < (int)y1; ++yy) {
			int c = AsmSin((unsigned)(yy * 7 + p * 5));
			AlphaColor(ClampByte(120 + c), ClampByte(50 + c / 2), ClampByte(150 - c / 2),
				(uint8)110);
			StrokeLine(BPoint(0, yy), BPoint(w, yy));
		}
		SolidMode();
	}

	void DrawScroller(const char* text, float y, unsigned p)
	{
		BFont font(be_bold_font);
		font.SetSize(20);
		SetFont(&font);
		int len = (int)strlen(text);
		const float charW = 15.0f;
		float total = len * charW;
		float startX = Bounds().Width() - (float)((p * 3) % (unsigned)(total + Bounds().Width()));
		for (int i = 0; i < len; ++i) {
			float cx = startX + i * charW;
			if (cx < -charW || cx > Bounds().Width())
				continue;
			float wob = AsmSin((unsigned)(p * 3 + i * 18)) / 6.0f;
			char c[2] = { text[i], 0 };
			SetHighColor(0, 0, 0, 255); // drop shadow
			DrawString(c, BPoint(cx + 2, y + wob + 2));
			int hue = AsmSin((unsigned)(p * 4 + i * 12));
			SetHighColor(ClampByte(180 + hue / 2), ClampByte(180 - hue / 3),
				ClampByte(120 + hue / 2), 255);
			DrawString(c, BPoint(cx, y + wob));
		}
		SetFont(be_plain_font);
	}

	BMessageRunner* fRunner = nullptr;
	unsigned        fPhase = 0;
};

// -------------------------------------------------------------------- chiptune synth (Media Kit)
//
// An ORIGINAL jaunty tune in a D-dorian, steel-drum spirit. The lead is a plucked sine with a
// couple of harmonics; a simple square bass walks underneath. Synthesised live, sample by sample,
// in the BSoundPlayer callback. This is not a copy of any copyrighted melody.

static const double kPi = 3.14159265358979323846;

// Lead melody as MIDI note numbers (0 == rest), two 16-step phrases (eighth notes).
static const int kMelody[32] = {
	69, 74, 77, 81, 79, 77, 76, 74,   72, 74, 76, 72, 69, 74,  0,  0,
	77, 76, 74, 72, 74, 77, 81,  0,   79, 77, 76, 74, 72, 69, 74,  0 };
// Walking bass, one note per two steps (16 entries over the 32-step loop).
static const int kBass[16] = {
	50, 53, 55, 57, 53, 50, 55, 57,   53, 50, 55, 57, 53, 45, 50, 57 };

struct ChipTune {
	double melPhase  = 0.0;
	double bassPhase = 0.0;
	double melFreq   = 0.0;
	double bassFreq  = 0.0;
	int    lastStep  = -1;
	uint64 sample    = 0;
	float  rate      = 44100.0f;
};

static double MidiFreq(int midi)
{
	if (midi <= 0)
		return 0.0;
	return 440.0 * pow(2.0, (midi - 69) / 12.0);
}

static void FillAudio(void* cookie, void* buffer, size_t size, const media_raw_audio_format&)
{
	ChipTune* t = (ChipTune*)cookie;
	float* out = (float*)buffer;
	size_t frames = size / sizeof(float);
	const double rate = t->rate;
	const int samplesPerStep = (int)(rate * 60.0 / 132.0 / 2.0); // 132 BPM, eighth notes

	for (size_t i = 0; i < frames; ++i) {
		int step = (int)((t->sample / samplesPerStep) % 32);
		double pos = (double)(t->sample % samplesPerStep) / samplesPerStep; // 0..1 within step
		if (step != t->lastStep) {
			t->melFreq = MidiFreq(kMelody[step]);
			t->bassFreq = MidiFreq(kBass[step / 2]);
			t->lastStep = step;
		}

		double lead = 0.0;
		if (t->melFreq > 0.0) {
			double env = exp(-pos * 3.0);            // plucked decay
			if (pos < 0.02) env *= pos / 0.02;        // tiny attack, avoids a click
			lead = env * (sin(t->melPhase) + 0.35 * sin(2 * t->melPhase)
				+ 0.12 * sin(3 * t->melPhase));
		}
		double bass = 0.0;
		if (t->bassFreq > 0.0) {
			double benv = 0.55 + 0.45 * exp(-pos * 1.4);
			double sq = sin(t->bassPhase) >= 0.0 ? 1.0 : -1.0;
			bass = benv * (0.6 * sq + 0.4 * sin(t->bassPhase));
		}

		out[i] = (float)(0.20 * lead + 0.11 * bass);

		t->melPhase += 2.0 * kPi * t->melFreq / rate;
		t->bassPhase += 2.0 * kPi * t->bassFreq / rate;
		if (t->melPhase > 2.0 * kPi) t->melPhase -= 2.0 * kPi;
		if (t->bassPhase > 2.0 * kPi) t->bassPhase -= 2.0 * kPi;
		++t->sample;
	}
}

// ------------------------------------------------------------------------------- the demo window

class DemoWindow : public BWindow {
public:
	DemoWindow()
		: BWindow(BRect(0, 0, 639, 399), "Campiello - Old School Demo", B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
	{
		AddChild(new DemoView(Bounds()));
		CenterOnScreen();
		StartMusic();
	}
	~DemoWindow() override
	{
		if (fPlayer != nullptr) {
			fPlayer->Stop();
			delete fPlayer;
		}
		delete fTune;
	}

private:
	void StartMusic()
	{
		fTune = new ChipTune();
		media_raw_audio_format fmt;
		memset(&fmt, 0, sizeof(fmt));
		fmt.frame_rate = fTune->rate;
		fmt.channel_count = 1;
		fmt.format = media_raw_audio_format::B_AUDIO_FLOAT;
		fmt.byte_order = B_MEDIA_HOST_ENDIAN;
		fmt.buffer_size = 2048;
		fPlayer = new BSoundPlayer(&fmt, "Campiello demo", FillAudio, NULL, fTune);
		if (fPlayer->InitCheck() != B_OK || fPlayer->Start() != B_OK) {
			delete fPlayer; // no media server (or headless): run the demo silently
			fPlayer = nullptr;
			return;
		}
		fPlayer->SetVolume(0.6f);
	}

	BSoundPlayer* fPlayer = nullptr;
	ChipTune*     fTune = nullptr;
};

BWindow* CreateOldSchoolDemoWindow()
{
	return new DemoWindow();
}
