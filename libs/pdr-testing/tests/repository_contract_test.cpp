#include <pdr/testing/repository_contract.hpp>

#include "fakes/fake_worlds.hpp"

namespace pdr::testing {

/// Первый прогон contract-набора: против фейка. Второй — против настоящего
/// адаптера Postgres, он живёт в цели pdr_postgres_contract_tests и требует базы
/// (docs/testing.md).
PDR_REPOSITORY_CONTRACT(Fake, FakeRepositoryWorld);

}  // namespace pdr::testing
