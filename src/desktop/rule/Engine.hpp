#pragma once

#include "Rule.hpp"
#include "../../helpers/signal/Signal.hpp"

namespace Desktop::Rule {
    class CRuleEngine {
      public:
        CRuleEngine();
        ~CRuleEngine() = default;

        void                          registerRule(SP<IRule>&& rule);
        void                          unregisterRule(const std::string& name);
        void                          unregisterRule(const SP<IRule>& rule);
        void                          updateAllRules();
        void                          cleanExecRules();
        void                          clearAllRules();
        const std::vector<SP<IRule>>& rules();

      private:
        std::vector<SP<IRule>> m_rules;
        CHyprSignalListener    m_targetsUpdatedHook;
        bool                   m_ruleUpdatePending = false;
    };

    SP<CRuleEngine> ruleEngine();
}