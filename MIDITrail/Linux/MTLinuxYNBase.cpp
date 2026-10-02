#include "MTLinuxCompat.h"
#include "../../YNBaseLib/YNErrCtrl.h"
#include "../../YNBaseLib/YNErrInfo.h"

namespace YNBaseLib {
YNErrInfo::YNErrInfo(ErrLevel level, unsigned long line, const TCHAR* function,
                     const TCHAR* message, unsigned long long info1,
                     unsigned long long info2)
    : m_ErrLevel(level), m_LineNo(line), m_ErrInfo1(info1), m_ErrInfo2(info2),
      m_FuncName(function ? function : ""), m_Message(message ? message : "") {}
YNErrInfo::~YNErrInfo() = default;
YNErrInfo::ErrLevel YNErrInfo::GetErrLevel() { return m_ErrLevel; }
unsigned long YNErrInfo::GetLineNo() { return m_LineNo; }
const TCHAR* YNErrInfo::GetFuncName() { return m_FuncName.c_str(); }
const TCHAR* YNErrInfo::GetMessage() { return m_Message.c_str(); }
unsigned long long YNErrInfo::GetErrInfo1() { return m_ErrInfo1; }
unsigned long long YNErrInfo::GetErrInfo2() { return m_ErrInfo2; }

static thread_local YNErrInfo* g_error = nullptr;
YNErrCtrl::YNErrCtrl() = default;
YNErrCtrl::~YNErrCtrl() = default;
int YNErrCtrl::Initialize() { return 0; }
int YNErrCtrl::Terminate() { delete g_error; g_error = nullptr; return 0; }
int YNErrCtrl::SetErr(YNErrInfo::ErrLevel level, unsigned long line,
                      const TCHAR* function, const TCHAR* message,
                      unsigned long long info1, unsigned long long info2) {
    delete g_error;
    g_error = new YNErrInfo(level, line, function, message, info1, info2);
    return -1;
}
YNErrInfo* YNErrCtrl::GetErr() { return g_error; }
int YNErrCtrl::ShowErr(HWND) { return -1; }
}
