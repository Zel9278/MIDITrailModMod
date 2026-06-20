//******************************************************************************
//
// MIDITrail / MTKeyboard11
//
// DX11 piano keyboard (M3 / M4.6): faithful port of the DX9 MTPianoKeyboardMod
// + MTPianoKeyboardCtrlMod. Reuses MTPianoKeyboard's exact key geometry (white
// C/F/D/G/A/E/B + black shapes with texture UVs) and the HDKeyboard.png texture,
// and the Mod world transform that maps the keyboard's local frame into the note
// world (pitch -> Y) at the playback line.
//
// Multi-keyboard: the Mod scene runs in multi mode (one keyboard per active
// MIDI port, stacked along Y, capped at KeyboardMaxDispNum). Each sub-keyboard
// shares the geometry/texture but has its own per-key press state (only its
// port's notes) and its own base transform (GetKeyboardBasePos(index)).
//
//******************************************************************************

#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include "DXPrimitive11.h"
#include "MTPianoKeyboard.h"
#include "MTPianoKeyboardDesignMod.h"
#include "MTNoteDesign.h"
#include "MTNotePitchBend.h"
#include "SMIDILib.h"

using namespace SMIDILib;


// max simultaneously displayed keyboards. The multi path is capped by
// KeyboardMaxDispNum (<=16); keep this small - the SubKbd array is by value in
// MIDITrailApp (a stack object), so SM_MAX_PORT_NUM (256) here overflows the stack.
#define MTKBD11_MAX_KEYBOARDS  (16)


class MTKeyboard11
{
public:
	MTKeyboard11();
	virtual ~MTKeyboard11();

	int Create(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
			const TCHAR* pSceneName, SMSeqData* pSeqData, bool isSingleKeyboard = true);
	void Release();

	int DrawDX11(ID3D11DeviceContext* pContext, const DirectX::XMMATRIX& viewProj,
			const DirectX::XMFLOAT4& lightDir, float rollAngle);

	// current playback tick: drives the keyboard's X follow position AND the
	// key-press animation (tick-based, so it is robust to dropped note on/off
	// messages under heavy load).
	void SetCurTickTime(unsigned long curTickTime);
	void Reset();

	// Live monitor: drive key presses directly from real-time MIDI (no note list /
	// tick). Lights the single keyboard's key, tinted with the note color, until
	// the matching note-off. m_CurTickTime stays 0 in live, so a key is "down"
	// while its keyMaxEndTick is non-zero (0xFFFFFFFF here).
	void SetNoteOnLive(unsigned char portNo, unsigned char chNo, unsigned char noteNo);
	void SetNoteOffLive(unsigned char noteNo);
	void AllNoteOffLive();

	// per-channel pitch bend (not owned); the keyboard shifts in pitch by the
	// port's strongest channel bend, matching MTPianoKeyboardCtrlMod.
	void SetPitchBend(MTNotePitchBend* pPitchBend) { m_pPitchBend = pPitchBend; }

	bool IsReady() { return m_Ready; }

private:
	// compact per-note record (~16 B vs the 32 B SMNote) with the color
	// precomputed, so the full note-list copy is dropped after Create.
	struct KbdNote {
		unsigned long startTime;
		unsigned long endTime;
		unsigned long color;     // D3DCOLOR 0xAARRGGBB
		unsigned char noteNo;
	};

	// per-key active-note color tracking (so a key shows the latest sounding
	// note's color and reverts when it ends, instead of sticking on the last
	// note-on). Bounded; down/up still uses keyMaxEndTick (robust).
	static const int KBD11_COLOR_CAP = 8;
	struct ActiveCol { unsigned long endTime; unsigned long color; };

	// one keyboard (per active MIDI port in multi mode)
	struct SubKbd {
		DXPrimitive11 prim;             // own VB/IB (VB mutated for key presses)
		void* pWorkVerts;               // current VB contents (CPU mirror)
		int keyboardIndex;              // design index -> base position
		int portNo;                     // MIDI port (for pitch-bend lookup)
		bool dirty;
		// tick-based active-note tracking (per this keyboard's notes)
		KbdNote* pNotes;
		unsigned long noteCount;
		unsigned long nextNoteIdx;
		unsigned long lastTick;
		bool keyDown[SM_MAX_NOTE_NUM];
		unsigned long keyColor[SM_MAX_NOTE_NUM];
		unsigned long keyRenderedColor[SM_MAX_NOTE_NUM];
		unsigned long keyMaxEndTick[SM_MAX_NOTE_NUM];
		ActiveCol activeCol[SM_MAX_NOTE_NUM][KBD11_COLOR_CAP];
		unsigned char activeColNum[SM_MAX_NOTE_NUM];
	};

	MTPianoKeyboard m_Geom;                 // geometry generator (DX9 builders, device-free)
	MTPianoKeyboardDesignMod m_DesignMod;   // base position + resize ratio
	MTNoteDesign m_NoteDesign;              // playback position + world move
	ID3D11ShaderResourceView* m_pSRV;       // keyboard texture (owned, shared)
	MTNotePitchBend* m_pPitchBend;          // per-channel pitch bend (not owned)
	bool m_SingleKbd;                       // true = one merged keyboard; false = per port
	bool m_Ready;
	unsigned long m_CurTickTime;
	DirectX::XMFLOAT3 m_WorldMove;

	void* m_pBaseVerts;                     // unpressed geometry (shared CPU master copy)
	unsigned long m_VertexNum;

	SubKbd m_Subs[MTKBD11_MAX_KEYBOARDS];
	unsigned long m_NumKbd;

	int _ApplyKeyStates(ID3D11DeviceContext* pContext, SubKbd* pSub);
	void _AdvanceWindow(SubKbd* pSub, unsigned long tick);
	void _ReleaseSub(SubKbd* pSub);
};
