"""Периодическое задание: отрабатывает и проверяется ТОЧКОЙ КОНТРОЛЯ, а не сном.

Механизм одиночных заданий написан давно и до первого процесса ни разу не
работал. Здесь он работает: воркер строит план, ставит след и производит
действие — а набор дожидается точки контроля внутри действия.

Сна в наборе нет и быть не может: `scripts/check_testsuite.py` его ловит. И это
не педантизм: сон делает набор одновременно медленным и ненадёжным, потому что
угаданное время ожидания однажды окажется меньше настоящего.
"""

import pytest


async def test_job_runs_and_says_so(service_client, testpoint):
    @testpoint('heartbeat-performed')
    def performed(data):
        pass

    await service_client.run_task('heartbeat-job')

    said = await performed.wait_call()
    assert said['data']['key'].startswith('heartbeat:')


async def test_effect_is_left_once_per_key(service_client, testpoint, pgsql):
    """Идемпотентность: второй прогон в тот же час действия не повторяет.

    След ставится ДО действия и в той же транзакции, поэтому «уже сделано»
    отличается от «ещё не сделано» самой базой, а не памятью процесса.
    """
    @testpoint('heartbeat-performed')
    def performed(data):
        pass

    await service_client.run_task('heartbeat-job')
    await performed.wait_call()

    await service_client.run_task('heartbeat-job')

    rows = pgsql['pdr'].cursor()
    rows.execute("select count(*) from jobs_effect where job = 'heartbeat-job'")
    assert rows.fetchone()[0] == 1, 'след поставлен второй раз'
    assert performed.times_called == 0, 'действие повторилось при том же ключе'


@pytest.mark.now('2027-03-01T12:00:00+00:00')
async def test_next_hour_is_a_new_key(service_client, testpoint, mocked_time):
    """Новый час — новый ключ, и действие обязано случиться снова.

    ВРЕМЯ ДВИГАЕТСЯ ШТАТНО. У периодического задания времени портом нет — его
    берёт фреймворк, — и подменяется оно фикстурой `mocked_time`, а не
    ожиданием следующего часа (docs/testing.md).
    """
    @testpoint('heartbeat-performed')
    def performed(data):
        pass

    await service_client.run_task('heartbeat-job')
    first = await performed.wait_call()

    mocked_time.sleep(3600)
    await service_client.update_server_state()

    await service_client.run_task('heartbeat-job')
    second = await performed.wait_call()

    assert second['data']['key'] != first['data']['key']
