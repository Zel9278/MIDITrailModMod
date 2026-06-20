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
	m_Ready = false;
	m_CurTickTime = 0;
	m_WorldMove = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_pBaseVerts = NULL;
	m_VertexNum = 0;
	m_NumKbd = 0;
	for (unsigned long i = 0; i < MTKBD11_MAX_KEYBOARDS; i++) {
		m_Subs[i].pWorkVerts = NULL;
		m_Subs[i].keyboardIndex = 0;
		m_Subs[i].portNo = 0;
		m_Subs[i].dirty = false;
		m_Subs[i].pNotes = NULL;
		m_Subs[i].noteCount = 0;
		m_Subs[i].nextNoteIdx = 0;
		m_Subs[i].lastTick = 0;
		ZeroMemory(m_Subs[i].keyDown, sizeof(m_Subs[i].keyDown));
		ZeroMemory(m_Subs[i].keyColor, sizeof(m_Subs[i].keyColor));
		ZeroMemory(m_Subs[i].keyRenderedColor, sizeof(m_Subs[i].keyRenderedColor));
		ZeroMemory(m_Subs[i].keyMaxEndTick, sizeof(m_Subs[i].keyMaxEndTick));
		ZeroMemory(m_Subs[i].activeColNum, sizeof(m_Subs[i].activeColNum));
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
	pSub->nextNoteIdx = 0;
	pSub->lastTick = 0;
	pSub->dirty = false;
	ZeroMemory(pSub->keyDown, sizeof(pSub->keyDown));
	ZeroMemory(pSub->keyColor, sizeof(pSub->keyColor));
	ZeroMemory(pSub->keyRenderedColor, sizeof(pSub->keyRenderedColor));
	ZeroMemory(pSub->keyMaxEndTick, sizeof(pSub->keyMaxEndTick));
	ZeroMemory(pSub->activeColNum, sizeof(pSub->activeColNum));
}

void MTKeyboard11::Release()
{
	for (unsigned long i = 0; i < MTKBD11_MAX_KEYBOARDS; i++) _ReleaseSub(&m_Subs[i]);
	m_NumKbd = 0;
	if (m_pSRV != NULL) { m_pSRV->Release(); m_pSRV = NULL; }
	if (m_pBaseVerts != NULL) { free(m_pBaseVerts); m_pBaseVerts = NULL; }
	m_VertexNum = 0;
	m_CurTickTime = 0;
	m_Ready = false;
}

void MTKeyboard11::Reset()
{
	m_CurTickTime = 0;
	for (unsigned long i = 0; i < m_NumKbd; i++) {
		m_Subs[i].nextNoteIdx = 0;
		m_Subs[i].lastTick = 0;
		ZeroMemory(m_Subs[i].keyMaxEndTick, sizeof(m_Subs[i].keyMaxEndTick));
		ZeroMemory(m_Subs[i].activeColNum, sizeof(m_Subs[i].activeColNum));
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
	m_Subs[0].activeColNum[noteNo] = 0;   // live path doesn't use the tick color list
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
// Fold every started note (startTime <= tick) into the per-key max end tick.
// A key is "down" while its max-end-tick is still ahead of the current tick, so
// missed note-off messages cannot leave a key stuck.
//******************************************************************************
void MTKeyboard11::_AdvanceWindow(SubKbd* pSub, unsigned long tick)
{
	// Only a genuine backward seek rebuilds the window (live tick jitters back a
	// few ticks; rebuilding per frame in a dense section stalls the keyboard).
	#define MTKBD11_SEEK_BACK_TICKS  (1920)   // ~a few beats
	if (tick + MTKBD11_SEEK_BACK_TICKS < pSub->lastTick) {
		pSub->nextNoteIdx = 0;
		ZeroMemory(pSub->keyMaxEndTick, sizeof(pSub->keyMaxEndTick));
	ZeroMemory(pSub->activeColNum, sizeof(pSub->activeColNum));
		pSub->lastTick = tick;
	}
	else if (tick > pSub->lastTick) {
		pSub->lastTick = tick;
	}

	while (pSub->nextNoteIdx < pSub->noteCount) {
		const KbdNote* pn = &pSub->pNotes[pSub->nextNoteIdx];
		if (pn->startTime > tick) break;
		if (pn->noteNo < SM_MAX_NOTE_NUM) {
			if (pn->endTime > pSub->keyMaxEndTick[pn->noteNo]) {
				pSub->keyMaxEndTick[pn->noteNo] = pn->endTime;
			}
			// push this note onto the key's active-color list (still-sounding only);
			// color = the most-recently-started active note, reverting as notes end.
			if (pn->endTime > tick) {
				unsigned char n = pSub->activeColNum[pn->noteNo];
				if (n >= KBD11_COLOR_CAP) {   // drop the oldest
					for (unsigned char k = 1; k < KBD11_COLOR_CAP; k++)
						pSub->activeCol[pn->noteNo][k - 1] = pSub->activeCol[pn->noteNo][k];
					n = KBD11_COLOR_CAP - 1;
				}
				pSub->activeCol[pn->noteNo][n].endTime = pn->endTime;
				pSub->activeCol[pn->noteNo][n].color   = pn->color;
				pSub->activeColNum[pn->noteNo] = n + 1;
			}
		}
		pSub->nextNoteIdx++;
	}
}

//******************************************************************************
// Apply pending key-press changes to one keyboard's vertex buffer
//******************************************************************************
int MTKeyboard11::_ApplyKeyStates(ID3D11DeviceContext* pContext, SubKbd* pSub)
{
	int result = 0;
	bool changed = false;
	unsigned char note;
	DXP11_VERTEX* pBase = (DXP11_VERTEX*)m_pBaseVerts;
	DXP11_VERTEX* pWork = (DXP11_VERTEX*)pSub->pWorkVerts;

	if (!pSub->dirty) return 0;
	if ((pBase == NULL) || (pWork == NULL)) { pSub->dirty = false; return 0; }

	for (note = 0; note < SM_MAX_NOTE_NUM; note++) {
		// drop notes that have ended from this key's active-color list, then take
		// the most-recently-started still-active note's color (revert on release).
		unsigned char an = pSub->activeColNum[note];
		if (an > 0) {
			unsigned char w = 0;
			for (unsigned char r = 0; r < an; r++) {
				if (pSub->activeCol[note][r].endTime > m_CurTickTime) {
					if (w != r) pSub->activeCol[note][w] = pSub->activeCol[note][r];
					w++;
				}
			}
			pSub->activeColNum[note] = w;
			if (w > 0) pSub->keyColor[note] = pSub->activeCol[note][w - 1].color;
		}

		bool wantDown = (pSub->keyMaxEndTick[note] > m_CurTickTime);

		bool needRebuild = (wantDown != pSub->keyDown[note])
		                || (wantDown && (pSub->keyColor[note] != pSub->keyRenderedColor[note]));
		if (!needRebuild) continue;

		unsigned long pos = 0, num = 0;
		m_Geom.GetKeyVertexRange(note, &pos, &num);
		if (num == 0) { pSub->keyDown[note] = wantDown; continue; }

		if (wantDown) {
			D3DXCOLOR col((D3DCOLOR)pSub->keyColor[note]);
			m_Geom.BuildKeyCPU(note, 1.0f, &col, &pWork[pos]);
			pSub->keyRenderedColor[note] = pSub->keyColor[note];
		}
		else {
			memcpy(&pWork[pos], &pBase[pos], (size_t)num * sizeof(DXP11_VERTEX));
		}
		pSub->keyDown[note] = wantDown;
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

	pSub->dirty = false;
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

	//----------------------------------
	// notes -> per-keyboard compact arrays (routed by port), color precomputed.
	// Live monitor (pSeqData == NULL): no song -> skip; keys are driven directly
	// by SetNoteOnLive/OffLive, so the per-keyboard note arrays stay empty.
	//----------------------------------
	if (pSeqData != NULL) {
		SMNote note;
		unsigned long counts[MTKBD11_MAX_KEYBOARDS];
		unsigned long fill[MTKBD11_MAX_KEYBOARDS];
		unsigned long total = 0;
		// track color mode: keep each note's source track so the pressed-key color
		// (ActiveKeyColorType=NOTE) matches the track-channel note color.
		bool trackMode = m_NoteDesign.IsTrackColorMode();
		SMNoteList* pNotes = NULL;
		const unsigned char* pTrackNo = NULL;

		for (i = 0; i < MTKBD11_MAX_KEYBOARDS; i++) { counts[i] = 0; fill[i] = 0; }

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

		// pass 1: count per keyboard
		for (i = 0; i < total; i++) {
			if (pNotes->GetNote(i, &note) != 0) { result = YN_SET_ERR("Program error.", i, 0); goto EXIT; }
			if (note.portNo >= SM_MAX_PORT_NUM) continue;
			int kbd = keyboardIndexForPort[note.portNo];
			if (kbd < 0) continue;
			counts[kbd]++;
		}
		// alloc
		for (i = 0; i < m_NumKbd; i++) {
			m_Subs[i].noteCount = counts[i];
			if (counts[i] > 0) {
				m_Subs[i].pNotes = (KbdNote*)malloc((size_t)counts[i] * sizeof(KbdNote));
				if (m_Subs[i].pNotes == NULL) { result = YN_SET_ERR("Could not allocate memory.", 0, 0); goto EXIT; }
			}
		}
		// pass 2: fill (note list is sorted by start tick -> each sub stays sorted)
		for (i = 0; i < total; i++) {
			if (pNotes->GetNote(i, &note) != 0) { result = YN_SET_ERR("Program error.", i, 0); goto EXIT; }
			if (note.portNo >= SM_MAX_PORT_NUM) continue;
			int kbd = keyboardIndexForPort[note.portNo];
			if (kbd < 0) continue;
			KbdNote* pn = &m_Subs[kbd].pNotes[fill[kbd]++];
			pn->startTime = note.startTime;
			pn->endTime   = note.endTime;
			pn->color     = trackMode
				? (D3DCOLOR)m_NoteDesign.GetTrackChannelColor(pTrackNo[i], note.chNo)
				: (D3DCOLOR)m_NoteDesign.GetNoteBoxColor(note.portNo, note.chNo, note.noteNo);
			pn->noteNo    = note.noteNo;
		}
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
// Draw: each keyboard with its own base transform + key-press state
//   world = Scale . Trans(basePos[idx]) . RotX(-90) . RotZ(90) . RotX(roll) . Trans(playbackPos)
//******************************************************************************
int MTKeyboard11::DrawDX11(
		ID3D11DeviceContext* pContext,
		const XMMATRIX& viewProj,
		const XMFLOAT4& lightDir,
		float rollAngle
	)
{
	if (!m_Ready) return 0;

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
		_ApplyKeyStates(pContext, pSub);

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
	}

	return 0;
}
