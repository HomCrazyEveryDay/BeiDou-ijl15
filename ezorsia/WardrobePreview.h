#pragma once
#include "WardrobeInput.h"

// Owns the actual 083 cash-shop preview user and movement controller. No CField is created.
namespace WardrobePreview {
bool Install();
void Uninstall();
bool Ready();
bool Busy();
void Create(void* windowLayer,const void* look);
void Dispose();
void SetLook(const void* look);
void Update(bool visible,bool interactive);
void SetScene(int scene);
int Scene();
void Jump();
void Attack();
void Stop();
void Key(unsigned key,unsigned flags);
// A nested socket/window callback must see the field, even while native preview code runs.
struct ExternalScope {
    WardrobeInput::FieldScope input;
    void* physics=nullptr;void* preview=nullptr;bool entered=false,switched=false;
    ExternalScope();~ExternalScope();
};
}
