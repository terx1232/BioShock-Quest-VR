#pragma once
#include <windows.h>

// Имена объектов движка (UE2.5 BioShock): таблица FName и поля UObject.
namespace uobject {
// Имя по индексу FName (nullptr - нет такого).
const wchar_t* NameByIndex(int index);
int FindName(const wchar_t* name); // existing names only; no engine mutation
// Имя объекта и имя его класса (nullptr, если не читается).
const wchar_t* ObjectName(void* object);
const wchar_t* ClassName(void* object);
// Смещение свойства (UnrealScript) в объекте по имени; -1 - нет такого.
int PropertyOffset(void* object, const wchar_t* name);
// Поле класса объекта (функция или свойство) по имени.
void* FindField(void* object, const wchar_t* name);
// Отладка: поля объекта, похожие на адреса кода Bioshock.exe.
void DumpCodePointers(void* obj, const char* label);
// Логическое свойство: смещение слова и маска бита (false - нет такого).
bool BoolProperty(void* object, const wchar_t* name, int& offset, unsigned& mask);
// Отладка: в лог поля объекта, похожие на FName и на указатели на объекты с именами.
void DumpCandidates(void* object, const char* label);
// Отладка: поля класса объекта, указывающие на объекты (ищем цепочку свойств).
void DumpClassLayout(void* object, const char* label);
// Отладка: все ссылки объекта на другие объекты (свойства ObjectProperty) с классами.
void DumpObjectRefs(void* object, const char* label);
// Отладка: имена костей скелетной сетки.
void DumpBoneNames(void* mesh);
void DumpPropertyFields(void* prop);
// Отладка: все свойства объекта (имя, тип, смещение, значение), чьё имя содержит filter (или все).
void DumpProperties(void* object, const char* label, const wchar_t* filter = nullptr);
}
