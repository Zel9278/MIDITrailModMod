#include "MTLinuxCompat.h"
#include "../../SMIDILib/SMFPUCtrl.h"

namespace SMIDILib {
SMFPUCtrl::SMFPUCtrl() : m_ThreadID(0), m_FPUCtrl(0), m_isLock(false) {}
SMFPUCtrl::~SMFPUCtrl() { m_isLock = false; }
int SMFPUCtrl::Start(FPUPrecision) { m_isLock = true; return 0; }
int SMFPUCtrl::End() { m_isLock = false; return 0; }
bool SMFPUCtrl::IsLocked() { return m_isLock; }
void SMFPUCtrl::_DisplayCurCtrl(TCHAR*) {}
}
