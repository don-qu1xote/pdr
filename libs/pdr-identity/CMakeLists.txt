# Контекст identity: люди, роли, тенанты, опека, согласия, дееспособность.
# Владение таблицами и фаза рождения — docs/architecture/context-map.md.
#
# Наружу видно РОВНО contract/identity/contract.hpp: каталог src приватный, и
# чужой модуль физически не может включить внутренность — не потому, что
# договорились, а потому, что её нет в его путях поиска.

add_library(pdr_identity STATIC
    src/identity/application/accept_invitation.cpp
    src/identity/application/announce_coming_of_age.cpp
    src/identity/application/attach_account.cpp
    src/identity/application/announce_guardian_handover.cpp
    src/identity/application/authenticate_session.cpp
    src/identity/application/change_password.cpp
    src/identity/application/confirm_email.cpp
    src/identity/application/contract_service.cpp
    src/identity/application/enrol_child.cpp
    src/identity/application/give_consent.cpp
    src/identity/application/grant_guardian_scope.cpp
    src/identity/application/invite_many.cpp
    src/identity/application/invite_participant.cpp
    src/identity/application/manage_publication.cpp
    src/identity/application/note_sensitive_access.cpp
    src/identity/application/notify_guardian_of_act.cpp
    src/identity/application/open_practice.cpp
    src/identity/application/show_my_data.cpp
    src/identity/application/policies/billing_policy.cpp
    src/identity/application/policies/capability_policy.cpp
    src/identity/application/policies/content_policy.cpp
    src/identity/application/policies/guardian_policy.cpp
    src/identity/application/policies/journal_policy.cpp
    src/identity/application/policies/matrix.cpp
    src/identity/application/policies/media_policy.cpp
    src/identity/application/policies/policy_set.cpp
    src/identity/application/policies/practice_policy.cpp
    src/identity/application/policies/progress_policy.cpp
    src/identity/application/policies/review_policy.cpp
    src/identity/application/policies/scheduling_policy.cpp
    src/identity/application/policies/subject.cpp
    src/identity/application/policies/subject_builder.cpp
    src/identity/application/register_on_my_own.cpp
    src/identity/application/reset_password.cpp
    src/identity/application/revoke_guardian_scope.cpp
    src/identity/application/revoke_guardianship.cpp
    src/identity/application/sign_in.cpp
    src/identity/application/show_access_journal.cpp
    src/identity/application/sign_out.cpp
    src/identity/core/access_record.cpp
    src/identity/core/account.cpp
    src/identity/core/action.cpp
    src/identity/core/age_status.cpp
    src/identity/core/auth_lifetimes.cpp
    src/identity/core/birth_date.cpp
    src/identity/core/contact_list.cpp
    src/identity/core/capabilities.cpp
    src/identity/core/digest.cpp
    src/identity/core/email.cpp
    src/identity/core/guardian_access.cpp
    src/identity/core/guardian_consent.cpp
    src/identity/core/guardian_scope.cpp
    src/identity/core/guardianship.cpp
    src/identity/core/consent.cpp
    src/identity/core/independent_act.cpp
    src/identity/core/login_throttle.cpp
    src/identity/core/membership.cpp
    src/identity/core/one_time_token.cpp
    src/identity/core/personal_data.cpp
    src/identity/core/password.cpp
    src/identity/core/practice.cpp
    src/identity/core/session.cpp
    src/identity/core/tenant.cpp
)
target_include_directories(pdr_identity
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/contract"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(pdr_identity PUBLIC pdr_core pdr_events PRIVATE pdr_application)

# Адаптеры к Postgres собираются только там, где есть userver. Без него
# контекст остаётся собираемым и проверяемым целиком, кроме этого слоя.
if(userver_FOUND)
    # Всё, что этот контекст кладёт в базу и берёт из неё: журнал доступа, вход,
    # роли. Таблицами identity_* владеет он один (docs/architecture/context-map.md).
    add_library(pdr_identity_infrastructure STATIC
        src/identity/infrastructure/access/dynamic_config_maturity_settings.cpp
        src/identity/infrastructure/access/logging_configuration_faults.cpp
        src/identity/infrastructure/access/postgres_access_journal.cpp
        src/identity/infrastructure/access/postgres_birth_dates.cpp
        src/identity/infrastructure/access/postgres_guardian_consents.cpp
        src/identity/infrastructure/access/postgres_guardianships.cpp
        src/identity/infrastructure/access/postgres_role_repository.cpp
        src/identity/infrastructure/audit/postgres_access_log.cpp
        src/identity/infrastructure/onboarding/postgres_accounts.cpp
        src/identity/infrastructure/onboarding/postgres_practices.cpp
        src/identity/infrastructure/onboarding/postgres_signup_attempts.cpp
        src/identity/infrastructure/auth/dynamic_config_auth_settings.cpp
        src/identity/infrastructure/auth/postgres_credential_store.cpp
        src/identity/infrastructure/auth/postgres_login_attempts.cpp
        src/identity/infrastructure/auth/postgres_one_time_tokens.cpp
        src/identity/infrastructure/auth/postgres_participant_directory.cpp
        src/identity/infrastructure/auth/postgres_session_store.cpp
        src/identity/infrastructure/auth/offloaded_password_hasher.cpp
        src/identity/infrastructure/auth/sha256_digests.cpp
        src/identity/infrastructure/component/permissions_component.cpp
    )
    # PUBLIC, а не PRIVATE: цель проверок адаптеров (pdr_adapter_tests) включает
    # заголовки отсюда. Наружу это ничего не открывает — чужой контекст с этой
    # целью не линкуется, а его попытку включить внутренность ловит
    # scripts/check_layers.py.
    target_include_directories(pdr_identity_infrastructure
                               PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(pdr_identity_infrastructure
                          PUBLIC pdr_identity pdr_postgres pdr_crypto pdr_infrastructure
                                 pdr_clock userver::postgresql)

    # Argon2id — отдельной целью, потому что ей нужна libargon2, а её в системе
    # может не быть. Своей схемы хеширования паролей в дереве нет и не будет:
    # «SHA с солью» подбирается на видеокарте миллиардами в секунду (ADR-0013),
    # поэтому при отсутствии библиотеки цель ПРОПУСКАЕТСЯ, а не подменяется
    # чем-нибудь попроще. В CI библиотека ставится (джоба adapters-with-userver),
    # то есть счёт паролей проверяется на каждом пуше.
    find_path(PDR_ARGON2_INCLUDE_DIR argon2.h)
    find_library(PDR_ARGON2_LIBRARY argon2)
    if(PDR_ARGON2_INCLUDE_DIR AND PDR_ARGON2_LIBRARY)
        message(STATUS "pdr: libargon2 найдена, счёт паролей собирается")
        add_library(pdr_identity_passwords STATIC
            src/identity/infrastructure/auth/argon2_password_hasher.cpp
        )
        target_include_directories(pdr_identity_passwords
                                   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
                                   PRIVATE "${PDR_ARGON2_INCLUDE_DIR}")
        target_link_libraries(pdr_identity_passwords
                              PUBLIC pdr_identity PRIVATE "${PDR_ARGON2_LIBRARY}")
    else()
        message(STATUS "pdr: libargon2 не найдена, счёт паролей пропущен")
    endif()

    # Транспорт контекста: опознание пришедшего и ручка входа.
    #
    # Отдельная цель, потому что она зависит от формы HTTP (pdr_http), а форма
    # зависит от публичного контракта identity. Цикла тут нет: контракт не знает
    # ни про форму, ни про эту цель. Собирается только со счётом паролей — вход
    # без него не бывает, а подменять Argon2id «чем-нибудь попроще» запрещено
    # (ADR-0013).
    if(TARGET pdr_identity_passwords)
        add_library(pdr_identity_http STATIC
            src/identity/infrastructure/http/session_callers.cpp
            src/identity/infrastructure/http/sign_in_operation.cpp
        )
        target_include_directories(pdr_identity_http
                                   PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(pdr_identity_http
                              PUBLIC pdr_identity_infrastructure pdr_identity_passwords
                                     pdr_http pdr_http_postgres pdr_crypto)
    endif()
endif()

if(PDR_BUILD_TESTS)
    # УРОВЕНЬ UNIT: домен и сценарии на фейках, всё в общей цели pdr_unit_tests.
    # Каталог tests тоже на путях включения: доменные билдеры (tests/builders/)
    # живут рядом со своим контекстом — платформенная оснастка зависеть от него
    # не имеет права.
    pdr_unit_test_sources(
        tests/access_log_test.cpp
        tests/adult_student_test.cpp
        tests/consent_test.cpp
        tests/age_status_test.cpp
        tests/capabilities_test.cpp
        tests/guardian_access_test.cpp
        tests/guardianship_test.cpp
        tests/invitation_test.cpp
        tests/login_throttle_test.cpp
        tests/onboarding_test.cpp
        tests/one_time_token_test.cpp
        tests/participants_test.cpp
        tests/password_test.cpp
        tests/permissions_matrix_test.cpp
        tests/policies_test.cpp
        tests/policy_registry_test.cpp
        tests/revoke_guardianship_test.cpp
        tests/session_transport_test.cpp
        tests/sign_in_test.cpp
    )
    pdr_unit_test_includes(src tests)
    pdr_unit_test_libraries(pdr_identity)

    # УРОВЕНЬ CONTRACT/ADAPTER: адаптеры против штатных заглушек userver и
    # против настоящей libargon2. Ни сервиса, ни базы им не нужно.
    if(userver_FOUND)
        pdr_adapter_test_sources(tests/dynamic_config_auth_settings_test.cpp
                                 tests/dynamic_config_maturity_settings_test.cpp)
        pdr_adapter_test_libraries(pdr_identity_infrastructure)

        if(TARGET pdr_identity_passwords)
            pdr_adapter_test_sources(tests/argon2_password_hasher_test.cpp)
            pdr_adapter_test_libraries(pdr_identity_passwords pdr_testing)
        endif()
    endif()
endif()
