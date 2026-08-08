#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelFireflow);
    p->addModel(modelGlow);
    p->addModel(modelFireflowHW);
}
