#pragma once

#include "Window.hpp"

#include "../../../managers/eventLoop/EventLoopTimer.hpp"

#include <string>
#include <vector>
#include <sys/types.h>

namespace Desktop::View {
    class CLaunchReservation {
      public:
        CLaunchReservation();

        void open(pid_t pid, const std::string& token);
        // The new window takes the reserved node. True means it is already in the layout.
        bool claim(PHLWINDOW window);
        bool ownsToken(const std::string& token) const;
        // An already-mapped window presented this launch's activation token.
        bool onActivation(const std::string& token, PHLWINDOW window);
        void forget(pid_t pid);

      private:
        struct SSlot {
            pid_t            pid = 0;
            std::string      token;
            PHLWINDOW       window;
            Time::steady_tp born;
        };

        PHLWINDOW match(PHLWINDOW window) const;
        SSlot*    slotForToken(const std::string& token);
        SSlot*    slotForWindow(PHLWINDOW window);
        void      dismiss(PHLWINDOW window, bool killProcess);
        void      retire(PHLWINDOW window);
        void      onTick();

        std::vector<SSlot> m_slots;
        SP<CEventLoopTimer> m_timer;
    };

    CLaunchReservation& launchReservations();
}
