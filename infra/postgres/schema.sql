-- The Game API applies this file at startup; it also runs directly via psql.
-- Keep it a single DO statement: all changes are atomic, and Drogon receives
-- one result without needing a SQL statement parser or a migration framework.
DO $schema$
BEGIN
    CREATE TABLE IF NOT EXISTS games (
        id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
        status text NOT NULL CHECK (status IN ('waiting', 'active', 'finished')),
        board text NOT NULL CHECK (char_length(board) = 64),
        current_turn text NOT NULL CHECK (current_turn IN ('white', 'black')),
        winner text CHECK (winner IN ('white', 'black')),
        version integer NOT NULL DEFAULT 1,
        created_at timestamptz NOT NULL DEFAULT now(),
        updated_at timestamptz NOT NULL DEFAULT now()
    );

    CREATE TABLE IF NOT EXISTS game_players (
        game_id uuid NOT NULL REFERENCES games (id) ON DELETE CASCADE,
        side text NOT NULL CHECK (side IN ('white', 'black')),
        token_hash text NOT NULL,
        PRIMARY KEY (game_id, side)
    );

    CREATE TABLE IF NOT EXISTS moves (
        id bigserial PRIMARY KEY,
        game_id uuid NOT NULL REFERENCES games (id) ON DELETE CASCADE,
        move_no integer NOT NULL,
        player_side text NOT NULL CHECK (player_side IN ('white', 'black')),
        from_cell integer NOT NULL CHECK (from_cell BETWEEN 0 AND 63),
        to_cell integer NOT NULL CHECK (to_cell BETWEEN 0 AND 63),
        created_at timestamptz NOT NULL DEFAULT now(),
        UNIQUE (game_id, move_no)
    );

    CREATE TABLE IF NOT EXISTS outbox_events (
        sequence bigserial PRIMARY KEY,
        event_id uuid NOT NULL UNIQUE DEFAULT gen_random_uuid(),
        game_id uuid NOT NULL,
        type text NOT NULL,
        request_id text NOT NULL,
        data jsonb NOT NULL DEFAULT '{}',
        created_at timestamptz NOT NULL DEFAULT now()
    );

    ALTER TABLE games ADD COLUMN IF NOT EXISTS capture_from integer
        CHECK (capture_from BETWEEN 0 AND 63);
    ALTER TABLE games ADD COLUMN IF NOT EXISTS captured_cells jsonb NOT NULL DEFAULT '[]'
        CHECK (jsonb_typeof(captured_cells) = 'array');
    ALTER TABLE moves ADD COLUMN IF NOT EXISTS captured_cell integer
        CHECK (captured_cell BETWEEN 0 AND 63);
    ALTER TABLE moves ADD COLUMN IF NOT EXISTS promoted boolean NOT NULL DEFAULT false;

    -- Upgrade games from the earlier one-step rules: crown only men already
    -- on their own promotion rank, not the opposing men starting there.
    UPDATE games
    SET board = regexp_replace(substr(board, 1, 8), 'w', 'W', 'g')
                || substr(board, 9, 48)
                || regexp_replace(substr(board, 57, 8), 'b', 'B', 'g'),
        version = version + 1,
        updated_at = now()
    WHERE substr(board, 1, 8) LIKE '%w%' OR substr(board, 57, 8) LIKE '%b%';
END;
$schema$;
