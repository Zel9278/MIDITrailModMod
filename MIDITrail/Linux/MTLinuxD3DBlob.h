#pragma once

#include <d3dcompiler.h>

#ifdef MIDITRAIL_LINUX_NATIVE
using MTD3DBlobGetBufferPointer = void* (__attribute__((ms_abi)) *)(ID3DBlob*);
using MTD3DBlobGetBufferSize = SIZE_T (__attribute__((ms_abi)) *)(ID3DBlob*);
using MTD3DBlobRelease = ULONG (__attribute__((ms_abi)) *)(ID3DBlob*);

inline void* MTGetD3DBlobBufferPointer(ID3DBlob* blob) {
    if (blob == nullptr) return nullptr;
    void** vtable = *reinterpret_cast<void***>(blob);
    return reinterpret_cast<MTD3DBlobGetBufferPointer>(vtable[3])(blob);
}

inline SIZE_T MTGetD3DBlobBufferSize(ID3DBlob* blob) {
    if (blob == nullptr) return 0;
    void** vtable = *reinterpret_cast<void***>(blob);
    return reinterpret_cast<MTD3DBlobGetBufferSize>(vtable[4])(blob);
}

inline ULONG MTReleaseD3DBlob(ID3DBlob* blob) {
    if (blob == nullptr) return 0;
    void** vtable = *reinterpret_cast<void***>(blob);
    return reinterpret_cast<MTD3DBlobRelease>(vtable[2])(blob);
}
#else
inline void* MTGetD3DBlobBufferPointer(ID3DBlob* blob) { return blob->GetBufferPointer(); }
inline SIZE_T MTGetD3DBlobBufferSize(ID3DBlob* blob) { return blob->GetBufferSize(); }
inline ULONG MTReleaseD3DBlob(ID3DBlob* blob) { return blob->Release(); }
#endif
