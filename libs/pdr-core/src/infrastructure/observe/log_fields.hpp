#pragma once

#include <string>

namespace pdr::infrastructure::observe {

/// ИМЕНА ПОЛЕЙ ЖУРНАЛА — ОДИН РАЗ И ЗДЕСЬ.
///
/// Поле, названное `tenant` в одном месте и `tenant_id` в другом, ищется двумя
/// запросами вместо одного, и выясняется это в час разбора. Отсюда константы:
/// имя пишется один раз, а расхождение с реестром (`configs/log-fields.yaml`)
/// роняет сборку в обе стороны — `scripts/check_log_fields.py`.
///
/// Заголовок без userver намеренно: имена нужны и тому, кто ставит тег спана, и
/// тому, кто собирает `LogExtra`, и обоим — из разных целей сборки. Тип
/// `std::string`, а не `std::string_view`, по той же причине: и `Span::AddTag`,
/// и `LogExtra::Pair` берут ключ строкой, а `string_view` в строку сам не
/// превращается — превращать пришлось бы на каждом месте вызова.
///
/// ПЕРСОНАЛЬНЫХ ДАННЫХ ЗДЕСЬ НЕТ И ЗАВЕСТИ ИХ НЕЛЬЗЯ: проверка смотрит на само
/// ИМЯ поля и роняет сборку на `email`, `phone`, `password` и прочем из перечня
/// (`docs/legal/personal-data.md`), не дожидаясь, пока в поле что-нибудь
/// положат.

/// Арендатор. Тег спана: ставится при открытии области и достаётся каждой
/// записи внутри неё.
inline const std::string kTenantField = "tenant_id";

/// Кто пришёл. Тег спана: ставится в форме запроса сразу после опознания.
inline const std::string kActorField = "actor_id";

inline const std::string kConfigKeyField = "config_key";
inline const std::string kConfigWasField = "config_was";
inline const std::string kConfigNowField = "config_now";
inline const std::string kConfigEntryField = "config_entry";
inline const std::string kConfigEntriesField = "config_entries";

inline const std::string kOutgoingDirectionField = "outgoing_direction";
inline const std::string kOutgoingFailureField = "outgoing_failure";
inline const std::string kRequestDeadlineField = "request_deadline_ms";

inline const std::string kJobNameField = "job_name";
inline const std::string kJobKeyField = "job_key";
inline const std::string kJobOutcomeField = "job_outcome";
inline const std::string kJobProducedField = "job_produced";
inline const std::string kJobRepeatedField = "job_repeated";
inline const std::string kJobFailureField = "job_failure";

inline const std::string kSecretsCheckedField = "secrets_checked";
inline const std::string kStorageFailureField = "storage_failure";
inline const std::string kPolicyActionField = "policy_action";
inline const std::string kAlertNameField = "alert_name";

}  // namespace pdr::infrastructure::observe
