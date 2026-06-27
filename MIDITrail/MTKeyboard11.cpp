//******************************************************************************
//
// MIDITrail / MTKeyboard11
//
// DX11 piano keyboard - faithful port of the DX9 MTPianoKeyboardMod +
// MTPianoKeyboardCtrlMod (M3 / M4.6 multi-keyboard).
//
//******************************************************************************

#include "stdafx.h"
#include "YNBaseLib.h"
#include "MTKeyboard11.h"
#include "DXTexture11.h"
#include <stdlib.h>

using namespace YNBaseLib;
using namespace DirectX;


MTKeyboard11::MTKeyboard11()
{
	m_pSRV = NULL;
	m_pPitchBend = NULL;
	m_SingleKbd = true;
	m_LiveMode = false;
	m_TickPerMs = 0.96;   // ~120bpm @ 480tpqn until the app feeds the real tempo scale
	m_Ready = false;
	m_CurTickTime = 0;
	m_WorldMove = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_pBaseVerts = NULL;
	m_VertexNum = 0;
	m_NumKbd = 0;
	m_InfiniteKbd = false;          //ced 20260629
	m_HasOctaveBlock = false;
	m_OctaveWidthX = 0.0f;
	ZeroMemory(m_OctaveKeyPrim, sizeof(m_OctaveKeyPrim));
	m_LastAnimMs = 0;
	m_KeyDownDurMs = 40;   // DX9 default; overridden from the design in Create
	m_KeyUpDurMs = 40;
	for (unsigned long i = 0; i < MTKBD11_MAX_KEYBOARDS; i++) {
		m_Subs[i].pWorkVerts = NULL;
		m_Subs[i].keyboardIndex = 0;
		m_Subs[i].portNo = 0;
		m_Subs[i].dirty = false;
		m_Subs[i].pNotes = NULL;
		m_Subs[i].noteCount = 0;
		m_Subs[i].lastTick = 0;
		ZeroMemory(m_Subs[i].keyOffset, sizeof(m_Subs[i].keyOffset));
		ZeroMemory(m_Subs[i].keyCursor, sizeof(m_Subs[i].keyCursor));
		ZeroMemory(m_Subs[i].keyDown, sizeof(m_Subs[i].keyDown));
		ZeroMemory(m_Subs[i].keyRate, sizeof(m_Subs[i].keyRate));
		ZeroMemory(m_Subs[i].keyColor, sizeof(m_Subs[i].keyColor));
		ZeroMemory(m_Subs[i].keyRenderedColor, sizeof(m_Subs[i].keyRenderedColor));
		ZeroMemory(m_Subs[i].keyMaxEndTick, sizeof(m_Subs[i].keyMaxEndTick));
	}
}

MTKeyboard11::~MTKeyboard11()
{
	Release();
}

void MTKeyboard11::_ReleaseSub(SubKbd* pSub)
{
	pSub->prim.Release();
	if (pSub->pWorkVerts != NULL) { free(pSub->pWorkVerts); pSub->pWorkVerts = NULL; }
	if (pSub->pNotes != NULL) { free(pSub->pNotes); pSub->pNotes = NULL; }
	pSub->noteCount = 0;
	pSub->lastTick = 0;
	pSub->dirty = false;
	ZeroMemory(pSub->keyOffset, sizeof(pSub->keyOffset));
	ZeroMemory(pSub->keyCursor, sizeof(pSub->keyCursor));
	ZeroMemory(pSub->keyDown, sizeof(pSub->keyDown));
	ZeroMemory(pSub->keyRate, sizeof(pSub->keyRate));
	ZeroMemory(pSub->keyColor, sizeof(pSub->keyColor));
	ZeroMemory(pSub->keyRenderedColor, sizeof(pSub->keyRenderedColor));
	ZeroMemory(pSub->keyMaxEndTick, sizeof(pSub->keyMaxEndTick));
}

void MTKeyboard11::Release()
{
	for (unsigned long i = 0; i < MTKBD11_MAX_KEYBOARDS; i++) _ReleaseSub(&m_Subs[i]);
	m_NumKbd = 0;
	if (m_pSRV != NULL) { m_pSRV->Release(); m_pSRV = NULL; }
	if (m_pBaseVerts != NULL) { free(m_pBaseVerts); m_pBaseVerts = NULL; }
	m_OctaveBlock.Release();        //ced 20260629
	m_HasOctaveBlock = false;
	m_InfiniteKbd = false;
	m_VertexNum = 0;
	m_CurTickTime = 0;
	m_Ready = false;
}

void MTKeyboard11::Reset()
{
	m_CurTickTime = 0;
	for (unsigned long i = 0; i < m_NumKbd; i++) {
		m_Subs[i].lastTick = 0;
		// rewind every key's note cursor to the start of its block (a seek can go back)
		for (unsigned long k = 0; k < SM_MAX_NOTE_NUM; k++) m_Subs[i].keyCursor[k] = m_Subs[i].keyOffset[k];
		ZeroMemory(m_Subs[i].keyMaxEndTick, sizeof(m_Subs[i].keyMaxEndTick));
		// Force every key to be rebuilt on the next draw (a seek/stop must not leave a
		// key visually stuck). The work vertices still hold the pre-seek pressed
		// geometry, so we set keyRate to an impossible sentinel: _ApplyKeyStates then
		// sees rate != keyRate for every key and rebuilds it (to base if released, or
		// to the new pressed state) instead of skipping it as "unchanged".
		for (unsigned char k = 0; k < SM_MAX_NOTE_NUM; k++) m_Subs[i].keyRate[k] = -1.0f;
		m_Subs[i].dirty = true;   // release all pressed keys on the next draw
	}
}

//******************************************************************************
// Live monitor key presses (single keyboard = sub 0)
//******************************************************************************
void MTKeyboard11::SetNoteOnLive(unsigned char portNo, unsigned char chNo, unsigned char noteNo)
{
	if ((m_NumKbd == 0) || (noteNo >= SM_MAX_NOTE_NUM)) return;
	// key stays down until note-off (m_CurTickTime is 0 in live, so any non-zero
	// keyMaxEndTick reads as "down" in _ApplyKeyStates).
	m_Subs[0].keyMaxEndTick[noteNo] = 0xFFFFFFFF;
	m_Subs[0].keyColor[noteNo] = (unsigned long)m_NoteDesign.GetNoteBoxColor(portNo, chNo, noteNo);
	m_Subs[0].dirty = true;
}

void MTKeyboard11::SetNoteOffLive(unsigned char noteNo)
{
	if ((m_NumKbd == 0) || (noteNo >= SM_MAX_NOTE_NUM)) return;
	m_Subs[0].keyMaxEndTick[noteNo] = 0;
	m_Subs[0].dirty = true;
}

void MTKeyboard11::AllNoteOffLive()
{
	if (m_NumKbd == 0) return;
	ZeroMemory(m_Subs[0].keyMaxEndTick, sizeof(m_Subs[0].keyMaxEndTick));
	m_Subs[0].dirty = true;
}

//******************************************************************************
// Current playback tick: advance every keyboard's active-note window
//******************************************************************************
void MTKeyboard11::SetCurTickTime(unsigned long curTickTime)
{
	m_CurTickTime = curTickTime;
	for (unsigned long i = 0; i < m_NumKbd; i++) {
		_AdvanceWindow(&m_Subs[i], curTickTime);
		m_Subs[i].dirty = true;
	}
}

//******************************************************************************
// Track the playback position. Per-key note scanning (the envelope) is done in
// _ApplyKeyStates straight from the per-key blocks + cursor; here we only rewind
// the cursors on a genuine backward seek so the scan can re-find the notes.
//******************************************************************************
void MTKeyboard11::_AdvanceWindow(SubKbd* pSub, unsigned long tick)
{
	// Only a genuine backward seek rewinds the cursors (live tick jitters back a few
	// ticks; rewinding every frame in a dense section would stall the keyboard).
	#define MTKBD11_SEEK_BACK_TICKS  (1920)   // ~a few beats
	if (tick + MTKBD11_SEEK_BACK_TICKS < pSub->lastTick) {
		for (unsigned long k = 0; k < SM_MAX_NOTE_NUM; k++) pSub->keyCursor[k] = pSub->keyOffset[k];
		ZeroMemory(pSub->keyMaxEndTick, sizeof(pSub->keyMaxEndTick));
		pSub->lastTick = tick;
	}
	else if (tick > pSub->lastTick) {
		pSub->lastTick = tick;
	}
}

//******************************************************************************
// Apply pending key-press changes to one keyboard's vertex buffer
//******************************************************************************
int MTKeyboard11::_ApplyKeyStates(ID3D11DeviceContext* pContext, SubKbd* pSub, unsigned long elapsedMs)
{
	int result = 0;
	bool changed = false;
	bool animating = false;   // any key still easing toward its target this frame
	unsigned char note;
	DXP11_VERTEX* pBase = (DXP11_VERTEX*)m_pBaseVerts;
	DXP11_VERTEX* pWork = (DXP11_VERTEX*)pSub->pWorkVerts;

	if (!pSub->dirty) return 0;
	if ((pBase == NULL) || (pWork == NULL)) { pSub->dirty = false; return 0; }

	// LIVE ease steps (wall-clock); PLAYBACK envelope durations (ticks at song tempo)
	float downStep = (m_KeyDownDurMs > 0) ? ((float)elapsedMs / (float)m_KeyDownDurMs) : 1.0f;
	float upStep   = (m_KeyUpDurMs   > 0) ? ((float)elapsedMs / (float)m_KeyUpDurMs)   : 1.0f;
	double downTicks = (double)m_KeyDownDurMs * m_TickPerMs;
	double upTicks   = (double)m_KeyUpDurMs   * m_TickPerMs;
	bool trackMode = m_NoteDesign.IsTrackColorMode();   // CHANNELTRACK: keep track colours

	for (note = 0; note < SM_MAX_NOTE_NUM; note++) {
		float rate = 0.0f;
		unsigned long useColor = pSub->keyColor[note];

		if (m_LiveMode) {
			// real-time: ease toward down/up (note-on sets keyMaxEndTick, m_CurTickTime=0)
			bool wantDown = (pSub->keyMaxEndTick[note] > m_CurTickTime);
			float target = wantDown ? 1.0f : 0.0f;
			rate = pSub->keyRate[note];
			if (rate < target)      { rate += downStep; if (rate > target) rate = target; }
			else if (rate > target) { rate -= upStep;   if (rate < target) rate = target; }
			if (rate != target) animating = true;
		}
		else {
			// PLAYBACK: DX9 anticipatory envelope evaluated over this key's note block.
			// The cursor skips notes whose release tail has fully passed; we then scan
			// forward over every note in the active window (start <= cur + downTicks) and
			// take the strongest (max-rate). No per-key cap -> a hammered key keeps its
			// sounding note (rate=1) however dense the stream.
			double cur = (double)m_CurTickTime;
			unsigned long lo = pSub->keyOffset[note];
			unsigned long hi = pSub->keyOffset[note + 1];
			unsigned long c = pSub->keyCursor[note];
			if (c < lo) c = lo;
			while (c < hi && (double)pSub->pNotes[c].endTime + upTicks < cur) c++;
			pSub->keyCursor[note] = c;
			unsigned char bestCh = 0;
			for (unsigned long j = c; j < hi; j++) {
				double s = (double)pSub->pNotes[j].startTime;
				if (s > cur + downTicks) break;   // beyond the anticipatory horizon (sorted by start)
				double e = (double)pSub->pNotes[j].endTime;
				float er;
				if (cur < s) {            // anticipatory press-down toward the note onset
					double d = s - cur;
					er = (d >= downTicks) ? 0.0f : (float)(1.0 - d / downTicks);
				}
				else if (cur <= e) er = 1.0f;                                  // held down
				else {                    // release ramp after note-off
					double d = cur - e;
					er = (d >= upTicks) ? 0.0f : (float)(1.0 - d / upTicks);
				}
				// >= so that among equally-pressed (overlapping) notes the LAST one in
				// the block - i.e. the most recent note-on - wins, layering the newest
				// note's colour on top (matches the previous behaviour). rate is the max
				// either way, so the press animation is unaffected.
				if (er >= rate) {
					rate = er;
					useColor = pSub->pNotes[j].color;
					bestCh = pSub->pNotes[j].chNo;
				}
			}
			// pressed-key colour: only the fully-pressed key is coloured. In track
			// colour mode (CHANNELTRACK) keep the per-track note colour; otherwise use
			// the [PianoKeyboard] ActiveKeyColor palette/type. elapsedTime = 0 so the
			// colour appears at full immediately on note-on (no Duration/TailRate
			// fade-in) and stays constant while held.
			if ((rate >= 1.0f) && !trackMode) {
				D3DXCOLOR noteCol((D3DCOLOR)useColor);
				useColor = (unsigned long)(D3DCOLOR)m_DesignMod.GetActiveKeyColor(bestCh, note, 0, &noteCol);
			}
		}

		// rebuild only when the rendered press depth or colour actually changes
		bool colorChanged = (rate > 0.0f) && (useColor != pSub->keyRenderedColor[note]);
		bool rateChanged  = (rate != pSub->keyRate[note]);
		if (!rateChanged && !colorChanged) continue;

		unsigned long pos = 0, num = 0;
		m_Geom.GetKeyVertexRange(note, &pos, &num);
		if (num == 0) { pSub->keyRate[note] = rate; continue; }

		if (rate >= 1.0f) {
			// fully pressed (note sounding): tilt + tint with the note colour
			D3DXCOLOR col((D3DCOLOR)useColor);
			m_Geom.BuildKeyCPU(note, rate, &col, &pWork[pos]);
			pSub->keyRenderedColor[note] = useColor;
			pSub->keyColor[note] = useColor;
		}
		else if (rate > 0.0f) {
			// DX9 only colours a fully-pressed key; during the down/up ramps the key
			// just rotates and stays the neutral key colour (NULL). So the note colour
			// snaps on exactly at note onset and off at note-off, not during the ramp.
			m_Geom.BuildKeyCPU(note, rate, NULL, &pWork[pos]);
			pSub->keyRenderedColor[note] = 0xFFFFFFFF;   // neutral; recolour when it reaches full press
		}
		else {
			memcpy(&pWork[pos], &pBase[pos], (size_t)num * sizeof(DXP11_VERTEX));
			pSub->keyRenderedColor[note] = 0xFFFFFFFF;   // force a colour rebuild on the next press
		}
		pSub->keyRate[note] = rate;
		pSub->keyDown[note] = (rate > 0.0f);
		changed = true;
	}

	if (changed) {
		DXP11_VERTEX* pv = NULL;
		result = pSub->prim.LockVertex(pContext, &pv);
		if (result == 0) {
			memcpy(pv, pWork, (size_t)m_VertexNum * sizeof(DXP11_VERTEX));
			pSub->prim.UnlockVertex(pContext);
		}
	}

	// keep the keyboard "dirty" while any key is still easing so DrawDX11 keeps
	// advancing the animation on the following frames (playback also re-dirties it
	// every tick; live mode relies on this to finish the ease between note events).
	pSub->dirty = animating;
	return result;
}

//******************************************************************************
// Create: generate the shared geometry/texture, then one keyboard per port
//******************************************************************************
int MTKeyboard11::Create(
		ID3D11Device* pDevice,
		ID3D11DeviceContext* pContext,
		const TCHAR* pSceneName,
		SMSeqData* pSeqData,
		bool isSingleKeyboard
	)
{
	int result = 0;
	unsigned long vn = 0;
	unsigned long in = 0;
	void* pCpuVB = NULL;
	unsigned long* pCpuIB = NULL;
	D3DXVECTOR3 mv;
	TCHAR texPath[_MAX_PATH] = { _T('\0') };
	unsigned int tw = 0, th = 0;
	SMPortList portList;
	int keyboardIndexForPort[SM_MAX_PORT_NUM];
	unsigned long i = 0;
	unsigned long maxDisp = 0;

	Release();

	result = DXPrimitive11::InitPipeline(pDevice);
	if (result != 0) goto EXIT;

	// designs (device-free)
	result = m_Geom.InitForDX11(pSceneName, pSeqData);
	if (result != 0) goto EXIT;
	result = m_DesignMod.Initialize(pSceneName, pSeqData);
	if (result != 0) goto EXIT;
	result = m_NoteDesign.Initialize(pSceneName, pSeqData);
	if (result != 0) goto EXIT;

	// key-press animation durations (DX9 [Keyboard] KeyDownDuration/KeyUpDuration, ms)
	m_KeyDownDurMs = m_DesignMod.GetKeyDownDuration();
	m_KeyUpDurMs   = m_DesignMod.GetKeyUpDuration();
	m_LastAnimMs   = timeGetTime();
	m_LiveMode     = (pSeqData == NULL);   // live monitor: no song -> wall-clock ease

	mv = m_NoteDesign.GetWorldMoveVector();
	m_WorldMove = XMFLOAT3(mv.x, mv.y, mv.z);

	//----------------------------------
	// keyboard count + port -> keyboard index map.
	// single: all ports/channels share ONE centered keyboard (SetKeyboardSingle
	// makes GetKeyboardBasePos(0) center it). multi: one keyboard per active port,
	// stacked (the original DX9 Mod behavior). Toggled from the View menu.
	//----------------------------------
	m_SingleKbd = isSingleKeyboard;
	for (i = 0; i < SM_MAX_PORT_NUM; i++) keyboardIndexForPort[i] = -1;

	if (isSingleKeyboard) {
		m_DesignMod.SetKeyboardSingle();
		for (i = 0; i < SM_MAX_PORT_NUM; i++) keyboardIndexForPort[i] = 0;  //all ports -> keyboard 0
		m_Subs[0].keyboardIndex = 0;
		m_Subs[0].portNo = 0;
		m_NumKbd = 1;
	}
	else {
		result = pSeqData->GetPortList(&portList);
		if (result != 0) goto EXIT;
		maxDisp = m_DesignMod.GetKeyboardMaxDispNum();
		m_NumKbd = 0;
		for (i = 0; i < portList.GetSize(); i++) {
			unsigned char portNo = 0;
			portList.GetPort(i, &portNo);
			if (portNo >= SM_MAX_PORT_NUM) continue;
			keyboardIndexForPort[portNo] = (int)m_NumKbd;
			m_Subs[m_NumKbd].keyboardIndex = (int)m_NumKbd;
			m_Subs[m_NumKbd].portNo = (int)portNo;
			m_NumKbd++;
			if (m_NumKbd >= maxDisp) break;
			if (m_NumKbd >= MTKBD11_MAX_KEYBOARDS) break;
		}
		if (m_NumKbd == 0) {   // no ports listed: fall back to a single keyboard
			keyboardIndexForPort[0] = 0;
			m_Subs[0].keyboardIndex = 0;
			m_Subs[0].portNo = 0;
			m_NumKbd = 1;
		}
	}

	//----------------------------------
	// shared geometry (one master CPU copy + the index buffer data)
	//----------------------------------
	m_Geom.GetGeometrySize(&vn, &in);
	if ((vn == 0) || (in == 0)) { result = 0; goto EXIT; }

	pCpuVB = malloc((size_t)vn * sizeof(DXP11_VERTEX));
	pCpuIB = (unsigned long*)malloc((size_t)in * sizeof(unsigned long));
	if ((pCpuVB == NULL) || (pCpuIB == NULL)) { result = YN_SET_ERR("Could not allocate memory.", 0, 0); goto EXIT; }

	result = m_Geom.BuildGeometryCPU(pCpuVB, pCpuIB);
	if (result != 0) goto EXIT;

	m_VertexNum = vn;
	m_pBaseVerts = malloc((size_t)vn * sizeof(DXP11_VERTEX));
	if (m_pBaseVerts == NULL) { result = YN_SET_ERR("Could not allocate memory.", 0, 0); goto EXIT; }
	memcpy(m_pBaseVerts, pCpuVB, (size_t)vn * sizeof(DXP11_VERTEX));

	// per-keyboard GPU buffers + CPU work mirror
	for (i = 0; i < m_NumKbd; i++) {
		DXP11_VERTEX* pv = NULL;
		unsigned long* pi = NULL;
		result = m_Subs[i].prim.CreateVertexBuffer(pDevice, vn);
		if (result != 0) goto EXIT;
		result = m_Subs[i].prim.CreateIndexBuffer(pDevice, in);
		if (result != 0) goto EXIT;
		result = m_Subs[i].prim.LockVertex(pContext, &pv);
		if (result != 0) goto EXIT;
		memcpy(pv, pCpuVB, (size_t)vn * sizeof(DXP11_VERTEX));
		m_Subs[i].prim.UnlockVertex(pContext);
		result = m_Subs[i].prim.LockIndex(pContext, &pi);
		if (result != 0) goto EXIT;
		memcpy(pi, pCpuIB, (size_t)in * sizeof(unsigned long));
		m_Subs[i].prim.UnlockIndex(pContext);
		m_Subs[i].prim.SetMaterialAmbient(0.55f, 0.55f, 0.55f);

		m_Subs[i].pWorkVerts = malloc((size_t)vn * sizeof(DXP11_VERTEX));
		if (m_Subs[i].pWorkVerts == NULL) { result = YN_SET_ERR("Could not allocate memory.", 0, 0); goto EXIT; }
		memcpy(m_Subs[i].pWorkVerts, pCpuVB, (size_t)vn * sizeof(DXP11_VERTEX));
	}

	//ced 20260629: 無限鍵盤（NotLive box 2D/3D のみ）。静的な1オクターブ分のタイルブロックを
	//作成しておき、描画時にオクターブ幅でカメラ可視範囲にタイルする。失敗時は無効化。
	m_InfiniteKbd = (m_DesignMod.IsInfiniteKeyboard() && !m_LiveMode);
	if (m_InfiniteKbd) {
		if (_BuildOctaveBlock(pDevice, pContext, pCpuVB, pCpuIB) != 0) {
			m_OctaveBlock.Release();
			m_HasOctaveBlock = false;
			m_InfiniteKbd = false;
		}
	}

	//----------------------------------
	// notes -> per-keyboard compact arrays (routed by port), color precomputed.
	// Live monitor (pSeqData == NULL): no song -> skip; keys are driven directly
	// by SetNoteOnLive/OffLive, so the per-keyboard note arrays stay empty.
	//----------------------------------
	if (pSeqData != NULL) {
		SMNote note;
		unsigned long counts[MTKBD11_MAX_KEYBOARDS];
		unsigned long total = 0;
		// track color mode: keep each note's source track so the pressed-key color
		// (ActiveKeyColorType=NOTE) matches the track-channel note color.
		bool trackMode = m_NoteDesign.IsTrackColorMode();
		SMNoteList* pNotes = NULL;
		const unsigned char* pTrackNo = NULL;

		for (i = 0; i < MTKBD11_MAX_KEYBOARDS; i++) counts[i] = 0;

		if (trackMode) {
			// shared, cached note list + per-note source track (built once)
			result = pSeqData->GetMergedNoteListWithTrack(&pNotes, &pTrackNo);
			if (result != 0) goto EXIT;
		}
		else {
			// shared, cached merged note list (built once across all components)
			result = pSeqData->GetMergedNoteList(&pNotes);
			if (result != 0) goto EXIT;
		}
		total = pNotes->GetSize();

		// pass 1: count notes per (keyboard, noteNo). keyOffset[n+1] becomes the per-key
		// block size, then a prefix sum turns it into the block start offset.
		for (i = 0; i < total; i++) {
			if (pNotes->GetNote(i, &note) != 0) { result = YN_SET_ERR("Program error.", i, 0); goto EXIT; }
			if (note.portNo >= SM_MAX_PORT_NUM) continue;
			if (note.noteNo >= SM_MAX_NOTE_NUM) continue;
			int kbd = keyboardIndexForPort[note.portNo];
			if (kbd < 0) continue;
			counts[kbd]++;
			m_Subs[kbd].keyOffset[note.noteNo + 1]++;
		}
		// alloc + prefix-sum the per-key block offsets; keyCursor doubles as the fill pos
		for (i = 0; i < m_NumKbd; i++) {
			m_Subs[i].noteCount = counts[i];
			if (counts[i] > 0) {
				m_Subs[i].pNotes = (KbdNote*)malloc((size_t)counts[i] * sizeof(KbdNote));
				if (m_Subs[i].pNotes == NULL) { result = YN_SET_ERR("Could not allocate memory.", 0, 0); goto EXIT; }
			}
			m_Subs[i].keyOffset[0] = 0;
			for (unsigned long k = 0; k < SM_MAX_NOTE_NUM; k++)
				m_Subs[i].keyOffset[k + 1] += m_Subs[i].keyOffset[k];
			for (unsigned long k = 0; k < SM_MAX_NOTE_NUM; k++)
				m_Subs[i].keyCursor[k] = m_Subs[i].keyOffset[k];   // running fill position
		}
		// pass 2: place each note into its key's block (the list is start-sorted, so each
		// block also comes out start-sorted)
		for (i = 0; i < total; i++) {
			if (pNotes->GetNote(i, &note) != 0) { result = YN_SET_ERR("Program error.", i, 0); goto EXIT; }
			if (note.portNo >= SM_MAX_PORT_NUM) continue;
			if (note.noteNo >= SM_MAX_NOTE_NUM) continue;
			int kbd = keyboardIndexForPort[note.portNo];
			if (kbd < 0) continue;
			KbdNote* pn = &m_Subs[kbd].pNotes[m_Subs[kbd].keyCursor[note.noteNo]++];
			pn->startTime = note.startTime;
			pn->endTime   = note.endTime;
			pn->color     = trackMode
				? (D3DCOLOR)m_NoteDesign.GetTrackChannelColor(pTrackNo[i], note.chNo)
				: (D3DCOLOR)m_NoteDesign.GetNoteBoxColor(note.portNo, note.chNo, note.noteNo);
			pn->noteNo    = note.noteNo;
			pn->chNo      = note.chNo;
		}
		// rewind the fill cursors back to each block's start for playback scanning
		for (i = 0; i < m_NumKbd; i++)
			for (unsigned long k = 0; k < SM_MAX_NOTE_NUM; k++)
				m_Subs[i].keyCursor[k] = m_Subs[i].keyOffset[k];
	}

	// keyboard texture (HDKeyboard.png) via WIC, shared across keyboards
	if (m_Geom.GetTexturePath(pSceneName, texPath, _MAX_PATH) == 0) {
		DXTexture11::LoadFromFile(pDevice, texPath, &m_pSRV, &tw, &th);
	}

	m_Ready = true;

EXIT:;
	if (pCpuVB != NULL) free(pCpuVB);
	if (pCpuIB != NULL) free(pCpuIB);
	return result;
}

//******************************************************************************
// ced 20260629: build the static one-octave tile block (notes 0-11, unpressed) for
// the infinite keyboard. Copies the octave's vertices from the CPU master and rebases
// its indices to start at 0. m_OctaveKeyPrim[k] = triangle offset of key boundary k.
//******************************************************************************
int MTKeyboard11::_BuildOctaveBlock(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
		const void* pCpuVB, const unsigned long* pCpuIB)
{
	int result = 0;
	const DXP11_VERTEX* pVB = (const DXP11_VERTEX*)pCpuVB;
	unsigned long vpos0 = 0, vnum0 = 0, vpos11 = 0, vnum11 = 0;
	unsigned long ipos0 = 0, inum0 = 0, ipos11 = 0, inum11 = 0;
	unsigned long vStart, vEnd, iStart, iEnd, blockVN, blockIN, n;
	unsigned long* pIdx = NULL;
	DXP11_VERTEX* pv = NULL;
	unsigned long* pi = NULL;
	int k;

	// one octave width (12 semitones) in local (pre-scale) X
	m_OctaveWidthX = m_DesignMod.GetKeyCenterPosX(12) - m_DesignMod.GetKeyCenterPosX(0);
	if (m_OctaveWidthX <= 0.0f) return -1;

	m_Geom.GetKeyVertexRange(0,  &vpos0,  &vnum0);
	m_Geom.GetKeyVertexRange(11, &vpos11, &vnum11);
	m_Geom.GetKeyIndexRange(0,   &ipos0,  &inum0);
	m_Geom.GetKeyIndexRange(11,  &ipos11, &inum11);
	vStart = vpos0;  vEnd = vpos11 + vnum11;
	iStart = ipos0;  iEnd = ipos11 + inum11;
	if ((vEnd <= vStart) || (iEnd <= iStart)) return -1;
	blockVN = vEnd - vStart;
	blockIN = iEnd - iStart;

	// per-key triangle offsets within the block (for the partial top tile = keys 8..11)
	for (k = 0; k <= 12; k++) {
		unsigned long ip = iEnd;
		if (k < 12) m_Geom.GetKeyIndexRange((unsigned char)k, &ip, NULL);
		m_OctaveKeyPrim[k] = (ip - iStart) / 3;
	}

	pIdx = (unsigned long*)malloc((size_t)blockIN * sizeof(unsigned long));
	if (pIdx == NULL) return YN_SET_ERR("Could not allocate memory.", 0, 0);
	for (n = 0; n < blockIN; n++) pIdx[n] = pCpuIB[iStart + n] - vStart;   // rebase indices

	if ((result = m_OctaveBlock.CreateVertexBuffer(pDevice, blockVN)) != 0) { free(pIdx); return result; }
	if ((result = m_OctaveBlock.CreateIndexBuffer(pDevice, blockIN)) != 0) { free(pIdx); return result; }
	if ((result = m_OctaveBlock.LockVertex(pContext, &pv)) != 0) { free(pIdx); return result; }
	memcpy(pv, &pVB[vStart], (size_t)blockVN * sizeof(DXP11_VERTEX));
	m_OctaveBlock.UnlockVertex(pContext);
	if ((result = m_OctaveBlock.LockIndex(pContext, &pi)) != 0) { free(pIdx); return result; }
	memcpy(pi, pIdx, (size_t)blockIN * sizeof(unsigned long));
	m_OctaveBlock.UnlockIndex(pContext);
	free(pIdx);

	m_OctaveBlock.SetMaterialAmbient(0.55f, 0.55f, 0.55f);
	m_HasOctaveBlock = true;
	return 0;
}

//******************************************************************************
// ced 20260629: tile the static octave block below note 0 and above note 127, within
// the camera's visible local-X range (so off-screen octaves are not drawn).
//******************************************************************************
void MTKeyboard11::_DrawInfiniteExtension(ID3D11DeviceContext* pContext, const XMMATRIX& mainWorld,
		const XMMATRIX& viewProj, const XMFLOAT4& lightDir, const XMFLOAT3& camPos)
{
	if (!m_HasOctaveBlock || (m_OctaveWidthX <= 0.0f)) return;

	// camera in keyboard-local (pre-transform) space, to cull octaves by local X.
	// Keys are laid out along local X (the pitch axis); the keyboard sits in the local
	// X-Y plane. The viewing distance that determines how wide an X span is on screen is
	// the camera's distance PERPENDICULAR to the pitch axis = sqrt(Y^2 + Z^2). (Using
	// only local Z collapsed to ~0 for some scene orientations -> the extension stopped
	// growing in that view.)
	XMVECTOR det;
	XMMATRIX inv = XMMatrixInverse(&det, mainWorld);
	XMVECTOR cl = XMVector3TransformCoord(XMVectorSet(camPos.x, camPos.y, camPos.z, 1.0f), inv);
	float camLocalX = XMVectorGetX(cl);
	float camLocalY = XMVectorGetY(cl);
	float camLocalZ = XMVectorGetZ(cl);

	// visible half-width along X grows with viewing distance (rough FOV proxy), capped
	// so the number of tiled octaves (= draw calls) stays bounded.
	float dist = sqrtf(camLocalY * camLocalY + camLocalZ * camLocalZ);
	float halfRange = dist * 1.3f + m_OctaveWidthX * 2.0f;
	if (halfRange > 160.0f) halfRange = 160.0f;
	float loX = camLocalX - halfRange;
	float hiX = camLocalX + halfRange;

	m_OctaveBlock.SetTexture(m_pSRV);

	// below note 0: full octaves k = -1, -2, ... (seamless; note 0 is C / octave boundary)
	for (int k = -1; k > -256; k--) {
		float ox = (float)k * m_OctaveWidthX;
		if (ox + m_OctaveWidthX < loX) break;     // fully left of the view
		m_OctaveBlock.SetWorldMatrix(XMMatrixTranslation(ox, 0.0f, 0.0f) * mainWorld);
		m_OctaveBlock.Draw(pContext, viewProj, lightDir, -1, 0);
	}

	// above note 127 (= octave 10, key G): fill keys 8..11 (notes 128..131) of octave 10
	// as a partial tile so there is no gap, then full octaves k = 11, 12, ...
	{
		float ox = 10.0f * m_OctaveWidthX;
		if ((ox + m_OctaveWidthX >= loX) && (ox <= hiX)) {
			int startPrim = (int)m_OctaveKeyPrim[8];
			int primCount = (int)m_OctaveKeyPrim[12] - startPrim;
			if (primCount > 0) {
				m_OctaveBlock.SetWorldMatrix(XMMatrixTranslation(ox, 0.0f, 0.0f) * mainWorld);
				m_OctaveBlock.Draw(pContext, viewProj, lightDir, primCount, startPrim);
			}
		}
	}
	for (int k = 11; k < 256; k++) {
		float ox = (float)k * m_OctaveWidthX;
		if (ox > hiX) break;                       // fully right of the view
		if (ox + m_OctaveWidthX < loX) continue;
		m_OctaveBlock.SetWorldMatrix(XMMatrixTranslation(ox, 0.0f, 0.0f) * mainWorld);
		m_OctaveBlock.Draw(pContext, viewProj, lightDir, -1, 0);
	}
}

//******************************************************************************
// Draw: each keyboard with its own base transform + key-press state
//   world = Scale . Trans(basePos[idx]) . RotX(-90) . RotZ(90) . RotX(roll) . Trans(playbackPos)
//******************************************************************************
int MTKeyboard11::DrawDX11(
		ID3D11DeviceContext* pContext,
		const XMMATRIX& viewProj,
		const XMFLOAT4& lightDir,
		float rollAngle,
		const XMFLOAT3& camPos
	)
{
	if (!m_Ready) return 0;

	// wall-clock elapsed since the previous frame, driving the key-press easing. Cap
	// it so a stall / pause (or the first frame) cannot snap every key in one step.
	unsigned long nowMs = timeGetTime();
	unsigned long elapsedMs = nowMs - m_LastAnimMs;
	if (elapsedMs > 100) elapsedMs = 100;
	m_LastAnimMs = nowMs;

	// normalize roll to [0,360) and pick the draw-face flip (matches MTPianoKeyboardMod)
	float roll = rollAngle;
	if (roll < 0.0f) roll += 360.0f;
	bool flipBack = (roll > 120.0f) && (roll < 300.0f);

	float scale = m_DesignMod.GetKeyboardResizeRatio();
	float playX = m_NoteDesign.GetPlayPosX(m_CurTickTime);

	XMMATRIX S  = XMMatrixScaling(scale, scale, scale);
	XMMATRIX R1 = XMMatrixRotationX(flipBack ? XM_PIDIV2 : -XM_PIDIV2);
	XMMATRIX R2 = XMMatrixRotationZ(XM_PIDIV2);
	XMMATRIX R3 = XMMatrixRotationX(XMConvertToRadians(rollAngle));
	XMMATRIX P  = XMMatrixTranslation(m_WorldMove.x + playX, m_WorldMove.y, m_WorldMove.z);

	for (unsigned long i = 0; i < m_NumKbd; i++) {
		SubKbd* pSub = &m_Subs[i];

		// push any pending key-press changes into this keyboard's vertex buffer
		_ApplyKeyStates(pContext, pSub, elapsedMs);

		D3DXVECTOR3 base = m_DesignMod.GetKeyboardBasePos(pSub->keyboardIndex, rollAngle);

		// pitch bend: shift in pitch (local X) by the strongest bend
		// (MTPianoKeyboardCtrlMod::GetMaxPitchBendShift). Single keyboard scans
		// every port; per-port keyboards scan just their own port's channels.
		if (m_pPitchBend != NULL) {
			float maxShift = 0.0f, cur = 0.0f;
			// NOTE: SM_MAX_PORT_NUM is 256, which overflows unsigned char to 0 -
			// a char loop bound made single-keyboard mode skip the scan entirely
			// (no bend). Use unsigned int for the port range.
			unsigned int pLo = m_SingleKbd ? 0u : (unsigned int)pSub->portNo;
			unsigned int pHi = m_SingleKbd ? (unsigned int)SM_MAX_PORT_NUM : (unsigned int)(pSub->portNo + 1);
			for (unsigned int p = pLo; p < pHi; p++) {
				for (unsigned char ch = 0; ch < SM_MAX_CH_NUM; ch++) {
					float s = m_DesignMod.GetPitchBendShift(
							m_pPitchBend->GetValue(p, ch),
							m_pPitchBend->GetSensitivity(p, ch));
					if (maxShift < (float)fabs(s)) { maxShift = (float)fabs(s); cur = s; }
				}
			}
			base.x += cur;
		}

		XMMATRIX B = XMMatrixTranslation(base.x, base.y, base.z);
		XMMATRIX world = S * B * R1 * R2 * R3 * P;
		pSub->prim.SetWorldMatrix(world);
		pSub->prim.SetTexture(m_pSRV);
		pSub->prim.Draw(pContext, viewProj, lightDir, -1, 0);

		//ced 20260629: 無限鍵盤 — このキーボード行の左右にオクターブブロックをタイル描画
		if (m_InfiniteKbd) {
			_DrawInfiniteExtension(pContext, world, viewProj, lightDir, camPos);
		}
	}

	return 0;
}
