#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace pdr::infrastructure::http {

/// Заголовок безопасности: имя и значение, оба постоянные.
struct SecurityHeader final {
    std::string_view name;
    std::string_view value;
};

/// Заголовки безопасности НА ВСЕХ ОТВЕТАХ, включая отказы.
///
/// «На всех» — не оборот речи: страница отказа, отданная без них, это ровно та
/// страница, которую покажут чужому. Поэтому список один, ставится он в
/// единственном месте — до всякой развилки в обработке, — и обойти его нечем.
///
/// Значения выбраны для API, отдающего JSON. Он не загружает ничего, никуда не
/// встраивается и ни одним устройством не пользуется, поэтому политика
/// содержимого запрещает всё: у API нет ни одного законного повода что-нибудь
/// подгрузить, а незаконный есть — отражённый ответ, который браузер решит
/// показать.
inline constexpr std::array<SecurityHeader, 4> kSecurityHeaders{
    SecurityHeader{"Content-Security-Policy",
                   "default-src 'none'; frame-ancestors 'none'; base-uri 'none'; "
                   "form-action 'none'"},
    SecurityHeader{"Referrer-Policy", "no-referrer"},
    SecurityHeader{"Permissions-Policy",
                   "accelerometer=(), camera=(), geolocation=(), gyroscope=(), "
                   "magnetometer=(), microphone=(), payment=(), usb=()"},
    SecurityHeader{"X-Content-Type-Options", "nosniff"},
};

/// Поставить их все. Шаблон по типу ответа: тестовый двойник — тип с тем же
/// `SetHeader`.
template<class Response>
void ApplySecurityHeaders(Response& response) {
    for (const auto& header : kSecurityHeaders) {
        response.SetHeader(std::string{header.name}, std::string{header.value});
    }
}

}  // namespace pdr::infrastructure::http
