-- Overflow-range invariant for the cash ledger: cash stays within +/- $100B
-- (1e13 cents), far inside int64 even after any sequence of capped orders.
--
-- Deliberately NOT cash_cents >= 0: Milestone 5's force-cover of a gapped
-- short can legitimately drive cash negative. Order paths enforce
-- cash' >= 0 in application code (order_math), not here.

ALTER TABLE users
    ADD CONSTRAINT users_cash_cents_bounds_check
    CHECK (cash_cents BETWEEN -10000000000000 AND 10000000000000);
