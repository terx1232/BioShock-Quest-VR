// Имена объектов BioShock: GNames - массив указателей на FNameEntry (Bioshock.exe 1.1,
// данные массива по адресу 0x112ba280, число элементов следом), имя - wchar_t с +0x10.
#include "uobject.h"

#include <stdio.h>

#include "log.h"

namespace uobject {
namespace {

const uintptr_t kNamesData = 0x112ba280;
const int kObjName = 0x24, kObjClass = 0x2c;  // UObject::Name / UObject::Class (проверено на PlayerController)

bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_READ))) return false;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

}  // namespace

const wchar_t* NameByIndex(int index) {
    int** data = *(int***)kNamesData;
    int count = *(int*)(kNamesData + 4);
    if (!data || index < 0 || index >= count || !Readable(data + index, 4)) return nullptr;
    int* entry = data[index];
    if (!entry || !Readable(entry, 0x14)) return nullptr;
    return (const wchar_t*)(entry + 4);
}

const wchar_t* ObjectName(void* object) {
    if (!object || !Readable((BYTE*)object + kObjName, 4)) return nullptr;
    return NameByIndex(*(int*)((BYTE*)object + kObjName));
}

int FindName(const wchar_t* name){
    if(!name||!Readable((void*)kNamesData,12))return -1;
    int count=*(int*)(kNamesData+4);if(count<1||count>500000)return -1;
    for(int i=0;i<count;i++){const wchar_t* value=NameByIndex(i);if(value&&!_wcsicmp(value,name))return i;}
    return -1;
}

const wchar_t* ClassName(void* object) {
    if (!object || !Readable((BYTE*)object + kObjClass, 4)) return nullptr;
    return ObjectName(*(void**)((BYTE*)object + kObjClass));
}

int PropertyOffset(void* object, const wchar_t* name) {
    // UClass: SuperField +0x38, Children +0x54; UField: Next +0x3c; UProperty: Offset +0x6c
    if (!object || !Readable((BYTE*)object + kObjClass, 4)) return -1;
    void* cls = *(void**)((BYTE*)object + kObjClass);
    for (int depth = 0; cls && depth < 32; depth++) {
        if (!Readable((BYTE*)cls + 0x54, 4)) return -1;
        void* f = *(void**)((BYTE*)cls + 0x54);
        for (int n = 0; f && n < 4096; n++) {
            if (!Readable((BYTE*)f + 0x70, 4)) break;
            const wchar_t* fn = ObjectName(f);
            if (fn && !_wcsicmp(fn, name)) {
                const wchar_t* cn = ClassName(f);
                if (cn && wcsstr(cn, L"Property")) return *(int*)((BYTE*)f + 0x6c);
            }
            f = *(void**)((BYTE*)f + 0x3c);
        }
        cls = *(void**)((BYTE*)cls + 0x38);
    }
    return -1;
}

void* FindField(void* object, const wchar_t* name) {
    if (!object || !Readable((BYTE*)object + kObjClass, 4)) return nullptr;
    void* cls = *(void**)((BYTE*)object + kObjClass);
    for (int depth = 0; cls && depth < 32; depth++) {
        if (!Readable((BYTE*)cls + 0x54, 4)) return nullptr;
        for (void* f = *(void**)((BYTE*)cls + 0x54); f && Readable((BYTE*)f + 0x40, 4); f = *(void**)((BYTE*)f + 0x3c)) {
            const wchar_t* fn = ObjectName(f);
            if (fn && !_wcsicmp(fn, name)) return f;
        }
        cls = *(void**)((BYTE*)cls + 0x38);
    }
    return nullptr;
}

void DumpCodePointers(void* obj, const char* label) {
    Log("Obj: %s %p '%ls' (%ls)", label, obj, obj ? ObjectName(obj) : L"-", obj ? ClassName(obj) : L"-");
    for (int off = 0x28; obj && off < 0xa0; off += 4) {
        if (!Readable((BYTE*)obj + off, 4)) break;
        DWORD v = *(DWORD*)((BYTE*)obj + off);
        if (v >= 0x10901000 && v < 0x10f00000) Log("Obj:   +0x%02x = %08lx (code?)", off, v);
    }
}

void DumpObjectRefs(void* object, const char* label) {
    if (!object || !Readable((BYTE*)object + kObjClass, 4)) return;
    Log("Obj: %s %p (%ls) object references:", label, object, ClassName(object));
    void* cls = *(void**)((BYTE*)object + kObjClass);
    for (int depth = 0; cls && depth < 32; depth++) {
        if (!Readable((BYTE*)cls + 0x54, 4)) return;
        for (void* f = *(void**)((BYTE*)cls + 0x54); f && Readable((BYTE*)f + 0x70, 4); f = *(void**)((BYTE*)f + 0x3c)) {
            const wchar_t* cn = ClassName(f);
            if (!cn || wcscmp(cn, L"ObjectProperty")) continue;
            int off = *(int*)((BYTE*)f + 0x6c);
            if (off <= 0 || off > 0x4000 || !Readable((BYTE*)object + off, 4)) continue;
            void* v = *(void**)((BYTE*)object + off);
            if (!v || !Readable((BYTE*)v + kObjClass, 4)) continue;
            Log("Obj:   %ls (+0x%x) -> %p %ls (vtable %08lx)", ObjectName(f), off, v, ClassName(v), *(DWORD*)v);
        }
        cls = *(void**)((BYTE*)cls + 0x38);
    }
}

void DumpBoneNames(void* mesh) {
    // ищем в объекте сетки массив (указатель, число, ёмкость), элементы которого начинаются с FName
    for (int off = 0x30; off < 0x600; off += 4) {
        if (!Readable((BYTE*)mesh + off, 12)) break;
        BYTE* data = *(BYTE**)((BYTE*)mesh + off);
        int num = *(int*)((BYTE*)mesh + off + 4), max = *(int*)((BYTE*)mesh + off + 8);
        if (num < 8 || num > 400 || max < num || max > 4096 || !Readable(data, 16)) continue;
        for (int stride = 0x20; stride <= 0x80; stride += 4) {
            if (!Readable(data + stride * (num - 1), 8)) break;
            int good = 0;
            for (int k = 0; k < num && k < 8; k++) {
                const wchar_t* n = NameByIndex(*(int*)(data + stride * k));
                if (n && *(int*)(data + stride * k) > 0) good++;
            }
            if (good < 8) continue;
            Log("Obj: mesh +0x%x: %d bones, stride 0x%x", off, num, stride);
            for (int k = 0; k < num; k++)
                Log("Obj:   bone %d %ls (+4=%d)", k, NameByIndex(*(int*)(data + stride * k)), *(int*)(data + stride * k + 4));
            return;
        }
    }
    Log("Obj: no bone array found in %p", mesh);
}

bool BoolProperty(void* object, const wchar_t* name, int& offset, unsigned& mask) {
    void* f = FindField(object, name);
    if (!f || !Readable((BYTE*)f + 0x94, 4)) return false;
    const wchar_t* cn = ClassName(f);
    if (!cn || wcscmp(cn, L"BoolProperty")) return false;
    offset = *(int*)((BYTE*)f + 0x6c);
    // BioShock extends UProperty beyond Offset. +0x70 is another field link,
    // not a flag mask. Verified on live ShockPlayer.bReticleDisabled: offset
    // +0xaa4, BitMask 0x4 at +0x94 (UBoolProperty size 0xa0).
    mask = *(unsigned*)((BYTE*)f + 0x94);
    return offset > 0 && mask && !(mask & (mask - 1));
}

void DumpClassLayout(void* object, const char* label) {
    void* cls = object && Readable((BYTE*)object + kObjClass, 4) ? *(void**)((BYTE*)object + kObjClass) : nullptr;
    Log("Obj: %s %p class %p '%ls'", label, object, cls, cls ? ObjectName(cls) : L"?");
    if (!cls) return;
    for (int off = 0; off < 0x120; off += 4) {
        if (!Readable((BYTE*)cls + off, 4)) break;
        void* p = *(void**)((BYTE*)cls + off);
        const wchar_t* cn = Readable(p, 0x30) ? ClassName(p) : nullptr;
        if (cn) Log("Obj:   class+0x%02x -> %p '%ls' (%ls)", off, p, ObjectName(p), cn);
    }
}

void DumpProperties(void* object, const char* label, const wchar_t* filter) {
    if (!object || !Readable((BYTE*)object + kObjClass, 4)) return;
    Log("Obj: %s %p (%ls) properties:", label, object, ClassName(object));
    void* cls = *(void**)((BYTE*)object + kObjClass);
    for (int depth = 0; cls && depth < 32; depth++) {
        if (!Readable((BYTE*)cls + 0x54, 4)) return;
        for (void* f = *(void**)((BYTE*)cls + 0x54); f && Readable((BYTE*)f + 0x70, 4); f = *(void**)((BYTE*)f + 0x3c)) {
            const wchar_t* cn = ClassName(f);
            const wchar_t* fn = ObjectName(f);
            if (!cn || !fn || !wcsstr(cn, L"Property")) continue;
            if (filter && !wcsstr(fn, filter)) continue;
            int off = *(int*)((BYTE*)f + 0x6c);
            if (off <= 0 || off > 0x4000 || !Readable((BYTE*)object + off, 12)) continue;
            BYTE* p = (BYTE*)object + off;
            if (!wcscmp(cn, L"FloatProperty")) Log("Obj:   %ls float +0x%x = %.3f", fn, off, *(float*)p);
            else if (!wcscmp(cn, L"NameProperty")) Log("Obj:   %ls name +0x%x = %d '%ls'", fn, off, *(int*)p, NameByIndex(*(int*)p));
            else if (!wcscmp(cn, L"ObjectProperty")) {
                void* v = *(void**)p;
                Log("Obj:   %ls object +0x%x = %p %ls", fn, off, v, v && Readable((BYTE*)v + kObjClass, 4) ? ClassName(v) : L"-");
            } else if (!wcscmp(cn, L"BoolProperty") && Readable((BYTE*)f+0x94,4)) Log("Obj:   %ls bool +0x%x mask %08x = %d", fn, off, *(unsigned*)((BYTE*)f + 0x94), (*(unsigned*)p & *(unsigned*)((BYTE*)f + 0x94)) != 0);
            else if (!wcscmp(cn, L"StructProperty")) Log("Obj:   %ls struct +0x%x = %.2f %.2f %.2f (%d %d %d)", fn, off, ((float*)p)[0], ((float*)p)[1], ((float*)p)[2], ((int*)p)[0], ((int*)p)[1], ((int*)p)[2]);
            else Log("Obj:   %ls %ls +0x%x = %d", fn, cn, off, *(int*)p);
        }
        cls = *(void**)((BYTE*)cls + 0x38);
    }
}

void DumpPropertyFields(void* prop) {
    Log("Obj: property %p '%ls' (%ls)", prop, ObjectName(prop), ClassName(prop));
    for (int off = 0x28; off < 0x70; off += 4) {
        if (!Readable((BYTE*)prop + off, 4)) break;
        int v = *(int*)((BYTE*)prop + off);
        void* p = (void*)(intptr_t)v;
        const wchar_t* cn = Readable(p, 0x30) ? ClassName(p) : nullptr;
        if (cn) Log("Obj:   +0x%02x -> '%ls' (%ls)", off, ObjectName(p), cn);
        else Log("Obj:   +0x%02x = %d (0x%x)", off, v, v);
    }
}

void DumpCandidates(void* object, const char* label) {
    Log("Obj: %s at %p, names count %d", label, object, *(int*)(kNamesData + 4));
    for (int off = 0; off <= 0x40; off += 4) {
        if (!Readable((BYTE*)object + off, 4)) continue;
        int v = *(int*)((BYTE*)object + off);
        const wchar_t* n = NameByIndex(v);
        if (n && v > 0) Log("Obj:   +0x%02x = name %d '%ls'", off, v, n);
        void* p = (void*)(intptr_t)v;
        if (Readable(p, 0x40)) {
            for (int o2 = 0x10; o2 <= 0x30; o2 += 4) {
                const wchar_t* n2 = NameByIndex(*(int*)((BYTE*)p + o2));
                if (n2 && *(int*)((BYTE*)p + o2) > 0) Log("Obj:   +0x%02x -> %p, its +0x%02x = '%ls'", off, p, o2, n2);
            }
        }
    }
}

}  // namespace uobject
