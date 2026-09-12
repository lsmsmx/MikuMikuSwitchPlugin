#include "Config.hpp"
#include "nc.hpp"
#include "diva_nc.hpp"
#include "game/game.hpp"
#include "input.hpp"
#include "db.hpp"
#include "game/dsc.hpp"
#include "game/target.hpp"
#include "mod.hpp"
#include "save_data.hpp"

#include "ui/pv_sel.hpp"
#include "ui/result.hpp"
#include "ui/customize_sel.hpp"


namespace nc {
    void init() {

        if (!Config::enableNewClassics) return;

        game::init();
        input::init();
        db::init();
        dsc::init();
        target::init();

        mod_nc::init();

        pvsel::init();
        CustomizeSelUi::Init();
        results::init();

        nc::init_save_data();

    }
}
