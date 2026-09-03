DELETE FROM identity_login_attempt
      WHERE subject_kind = $1
        AND subject_hash = $2
