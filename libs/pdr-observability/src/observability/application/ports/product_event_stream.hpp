#pragma once

#include "observability/core/product_event.hpp"

namespace pdr::observability::ports {

/// Узкий порт: положить запись в поток продуктовых событий.
///
/// Поток ОТДЕЛЬНЫЙ от технических метрик и попадает в свою таблицу
/// (`observability_product_event`). Задержки, коды ответов и длины очередей
/// живут штатными средствами userver и в базу не попадают вовсе: у двух потоков
/// разные читатели, разные права доступа и разный срок жизни, и в одном месте
/// пришлось бы выбрать один срок для обоих и ошибиться дважды.
///
/// Чтения здесь нет намеренно. Поток пишется приложением, а читается выгрузкой
/// (`make product-events-export`) — сервису читать его незачем, и порт, которого
/// нет, нельзя случайно позвать из сценария.
class ProductEventStream {
public:
    ProductEventStream(const ProductEventStream&) = delete;
    ProductEventStream& operator=(const ProductEventStream&) = delete;

    virtual ~ProductEventStream() = default;

    virtual void Record(const ProductEvent& event) = 0;

protected:
    ProductEventStream() = default;
};

}  // namespace pdr::observability::ports
