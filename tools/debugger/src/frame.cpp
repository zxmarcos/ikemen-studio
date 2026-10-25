#include "frame.h"

#include <cstring>
#include <string>

namespace {

const unsigned kMagic = 0x52464B49; // "IKFR"
const unsigned kVersion = 1;
const size_t kHeader = 4096;

unsigned Load(const unsigned char *p) { return *(const volatile unsigned *)p; }
void Store(unsigned char *p, unsigned v) { *(volatile unsigned *)p = v; }

} // namespace

bool FrameLink::Open(DWORD pid) {
    Close();
    std::wstring name = L"Local\\IkemenFrame" + std::to_wstring(pid);
    map_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str());
    if (!map_) return false;
    base_ = (unsigned char *)MapViewOfFile(map_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!base_ || Load(base_) != kMagic || Load(base_ + 4) != kVersion) {
        Close();
        return false;
    }
    lastCount_ = 0;
    return true;
}

void FrameLink::Close() {
    if (base_) UnmapViewOfFile(base_);
    if (map_) CloseHandle(map_);
    base_ = nullptr;
    map_ = nullptr;
}

bool FrameLink::Read(GameFrame &out) {
    if (!base_) return false;
    unsigned count = Load(base_ + 20);
    if (count == lastCount_) return false;
    unsigned slot = Load(base_ + 16) & 1;
    size_t slotSize = Load(base_ + 8), pixelBytes = Load(base_ + 12);
    const unsigned char *head = base_ + 64 + 32 * slot;
    unsigned seq = Load(head);
    if (seq & 1) return false; // being written: take it on the next poll
    MemoryBarrier();
    int w = (int)Load(head + 4), h = (int)Load(head + 8);
    size_t geomLen = Load(head + 12);
    if (w <= 0 || h <= 0 || (size_t)w * h * 4 > pixelBytes || geomLen > slotSize - pixelBytes) return false;
    const unsigned char *data = base_ + kHeader + slot * slotSize;
    out.pixels.resize((size_t)w * h * 4);
    memcpy(out.pixels.data(), data, out.pixels.size());
    out.geometry.assign((const char *)data + pixelBytes, geomLen);
    out.frame = Load(head + 16);
    MemoryBarrier();
    if (Load(head) != seq) return false; // overwritten while copied
    out.width = w;
    out.height = h;
    lastCount_ = count;
    return true;
}

bool FrameLink::SendEvent(int kind, int a, int b, int c) {
    if (!base_) return false;
    unsigned ring = Load(base_ + 24), size = Load(base_ + 28);
    unsigned written = Load(base_ + 32);
    if (!size || written - Load(base_ + 36) >= size) return false; // the game is not reading
    int *ev = (int *)(base_ + ring + 16 * (written % size));
    ev[0] = kind;
    ev[1] = a;
    ev[2] = b;
    ev[3] = c;
    MemoryBarrier();
    Store(base_ + 32, written + 1);
    return true;
}

bool FrameLink::SendKey(int sdlKey, bool down, int sdlMods) { return SendEvent(1, sdlKey, down ? 1 : 0, sdlMods); }