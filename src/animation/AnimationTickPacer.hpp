#pragma once

#include <chrono>

namespace Animation {
    class CAnimationTickPacer {
      public:
        void                      considerOutput(float refreshRate, bool enabled = true);
        std::chrono::microseconds interval() const;

      private:
        float m_fastestRefreshRate = 0.F;
    };
}
