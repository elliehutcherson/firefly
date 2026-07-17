-- Reject structurally invalid financial and market data at every write path.
-- These are domain invariants, not request validation or trading policy.

ALTER TABLE instruments
    ADD CONSTRAINT instruments_symbol_format_check
    CHECK (symbol ~ '^[A-Z][A-Z.-]{0,9}$');

ALTER TABLE candles_daily
    ADD CONSTRAINT candles_daily_prices_positive_check
    CHECK (
        open > 0 AND open <> 'NaN'::numeric AND
        high > 0 AND high <> 'NaN'::numeric AND
        low > 0 AND low <> 'NaN'::numeric AND
        close > 0 AND close <> 'NaN'::numeric
    ),
    ADD CONSTRAINT candles_daily_range_check
    CHECK (
        high >= GREATEST(open, low, close) AND
        low <= LEAST(open, high, close)
    ),
    ADD CONSTRAINT candles_daily_volume_nonnegative_check
    CHECK (volume >= 0);

ALTER TABLE orders
    ADD CONSTRAINT orders_price_positive_check
    CHECK (price > 0 AND price <> 'NaN'::numeric);

-- A closed position has no useful state and must be deleted, not retained as
-- a zero row. Both long and short positions have a positive acquisition price.
ALTER TABLE positions
    ADD CONSTRAINT positions_quantity_nonzero_check
    CHECK (quantity <> 0),
    ADD CONSTRAINT positions_avg_price_positive_check
    CHECK (avg_price > 0 AND avg_price <> 'NaN'::numeric);
