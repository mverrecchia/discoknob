#include "demo_apps.h"

DemoApps::DemoApps(TFT_eSprite *spr) : HassApps(spr)
{
    HassApps::sync(cJSON_Parse(R"([{"app_slug": "light_switch","app_id": "light_switch.couch","friendly_name": "Couch","area": "LivingRoom","menu_color": "#ffffff","entity_id": "couch_switch_entity_id"},
                                   {"app_slug": "light_switch","app_id": "light_switch.neon","friendly_name": "Neon","area": "LivingRoom","menu_color": "#ffffff","entity_id": "neon_switch_entity_id"},
                                   {"app_slug": "discoball","app_id": "discoball.ceiling","friendly_name": "","area": "LivingRoom","menu_color": "#ffffff","entity_id": "discoball_entity_id"}])"));
}
//    {"app_slug": "3d_printer","app_id": "3d_printer.office","friendly_name": "","area": "Office","menu_color": "#ffffff","entity_id": "printer_entity_id"},
//    {"app_slug": "thermostat","app_id": "climate.office","friendly_name": "","area": "Office","menu_color": "#ffffff","entity_id": "climate_entity_id"},
//    {"app_slug": "blinds","app_id": "blinds.office","friendly_name": "","area": "Office","menu_color": "#ffffff","entity_id": "blinds_entity_id"},
//    {"app_slug": "light_dimmer","app_id": "light.workbench","friendly_name": "","area": "Kitchen","menu_color": "#ffffff","entity_id": "workbench_light_entity_id"},
//    {"app_slug": "stopwatch","app_id": "stopwatch.office","friendly_name": "","area": "office","menu_color": "#ffffff","entity_id": "stopwatch_entity_id"},
//    {"app_slug": "climate","app_id": "climate.office","friendly_name": "","area": "office","menu_color": "#ffffff","entity_id": "climate_entity_id"},
//    {"app_slug": "music","app_id": "music.office","friendly_name": "","area": "Office","menu_color": "#ffffff","entity_id": "music_entity_id"},

void DemoApps::handleNavigationEvent(NavigationEvent event)
{
    // No need to handle mode switching since we only have Demo mode now
    Apps::handleNavigationEvent(event);
}

void DemoApps::setOSConfigNotifier(OSConfigNotifier *os_config_notifier)
{
    this->os_config_notifier = os_config_notifier;
}