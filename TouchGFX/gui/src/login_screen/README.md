# Login Screen

4-digit PIN entry screen. Authenticates the operator before allowing access to the QC workflow.

## Flow

1. Operator taps digit keys (1–9). Each tap fills one circle indicator.
2. On the 4th digit the PIN is validated automatically — no confirm button.
3. **Correct PIN** → navigate to Product Reference screen.
4. **Wrong PIN** → all four circles flash red for ~600 ms, then reset.

## MVP responsibilities

| Layer | File | Does |
|---|---|---|
| Model | `Model.cpp` | Holds the operator table; `validatePin()` compares the entered string against each operator's PIN |
| Presenter | `loginPresenter.cpp` | Owns the PIN buffer (`m_pin[5]`) and digit count; calls `submitPin()` on the 4th digit |
| View | `loginView.cpp` | Wires keypad callbacks; renders filled/outline circles via `setLineWidth`; runs the error tick countdown |

## Circle rendering

`Circle::setLineWidth(0)` → filled disc, PMP gold `#D4B765` (digit entered)  
`Circle::setLineWidth(2)` → outline ring, PMP gold (slot empty)  
Error state: painter color set to red `#E53E3E`, `setLineWidth(0)`, 36-tick countdown (~600 ms at 60 fps) then reset.

## PoC operators (hard-coded in `Model.cpp`)

| ID | Name | PIN |
|---|---|---|
| 1 | Mohammed Benali | 1234 |
| 2 | Karim Trabelsi | 5678 |
| 3 | Youssef Chabbi | 9876 |

Replace with `pin_hash_verify()` from `Application/domain/pin_hash.h` before production.
