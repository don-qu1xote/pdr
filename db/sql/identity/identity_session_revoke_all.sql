-- Гасятся только живые: у погашенной момент отзыва не переписывается, иначе
-- «когда этот доступ забрали» отвечает последняя смена пароля, а не первая.
UPDATE identity_session
   SET revoked_at = $2
 WHERE person_id = $1::uuid
   AND revoked_at IS NULL
