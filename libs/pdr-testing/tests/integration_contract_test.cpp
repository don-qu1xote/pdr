#include <string>

#include <pdr/testing/integration_contract.hpp>

/// @file
/// Образцовый мир для набора «сервиса нет навсегда»: он показывает, как
/// подключается интеграция, и доказывает, что набор проходим.
///
/// НАСТОЯЩЕЙ ИНТЕГРАЦИИ В ДЕРЕВЕ ПОКА НЕТ — ни платёжного провайдера, ни чеков,
/// ни LiveKit, ни провайдера моделей: в `libs/` только порты хранилищ. Поэтому
/// набор проверяется на своём образце, а не на живой интеграции. Требование
/// инстанцировать его для каждой настоящей держит scripts/check_integrations.py:
/// строка в docs/architecture/integrations.md без вызова
/// PDR_INTEGRATION_CONTRACT роняет сборку.
///
/// Образец нарочно тривиален: занятие назначается всегда, а внешний сервис лишь
/// добавляет к нему ссылку на комнату. Это и есть украшение по ADR-0014 —
/// функция, отсутствие которой продукт переживает.

namespace pdr::testing {
namespace {

struct Booking final {
    bool booked{false};
    std::string room;
    std::string explanation;
};

class RoomsWorld final {
public:
    using Outcome = Booking;

    void ServiceIsHealthy() {
        alive_ = true;
    }

    void ServiceIsGoneForever() {
        alive_ = false;
    }

    void IntegrationSwitchedOff() {
        enabled_ = false;
    }

    Outcome Run() {
        Booking booking;
        booking.booked = true;

        if (!enabled_) {
            booking.explanation =
                "Видеокомната отключена. Занятие назначено, ссылку пришлём "
                "отдельно или проведите занятие привычным способом.";
            return booking;
        }

        ++calls_;
        if (!alive_) {
            booking.explanation =
                "Видеокомнату сейчас не выдать. Занятие назначено — "
                "договоритесь о способе связи в переписке.";
            return booking;
        }

        booking.room = "https://video.example.test/room/1";
        return booking;
    }

    int CallsOutside() const noexcept {
        return calls_;
    }

    static bool ProductWorked(const Outcome& outcome) {
        return outcome.booked;
    }

    static bool FeaturePresent(const Outcome& outcome) {
        return !outcome.room.empty();
    }

    static std::string Explanation(const Outcome& outcome) {
        return outcome.explanation;
    }

private:
    bool alive_{true};
    bool enabled_{true};
    int calls_{0};
};

}  // namespace

PDR_INTEGRATION_CONTRACT(Example, RoomsWorld);

}  // namespace pdr::testing
