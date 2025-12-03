# Changelog — ButtonLib

Формат версий: [SemVer](https://semver.org/)  
Запись: `MAJOR.MINOR.PATCH`

---

## [3.1.0] — 2025-12-03

### Added
- Документ `docs/buttonlib_spec.md` с подробной спецификацией.
- Обновлённый `README.md` с описанием API, примерами и структурой репозитория.
- Набросок unit-тестов (`tests/buttonlib_test.c`) с симуляцией таймлайна.
- Диаграмма состояний (Mermaid) в документации.

### Changed
- Улучшено поведение `LONG_HOLD`:
  - глобальная статическая переменная `rep_cnt` заменена на поле `hold_repeat_count` в `btn_state_t`;
  - счётчик повторов сбрасывается при отпускании.
- `btn_suppress_events()` теперь обнуляет также счётчик удержания (`hold_repeat_count`).

### Fixed
- Потенциальная путаница между повторными срабатываниями `LONG_HOLD` разных кнопок.

---

## [3.0.0] — 2025-12-01

### Added
- Базовая версия библиотеки:
  - антидребезг;
  - поддержка `DOWN`, `UP`, `CLICK`, `LONG_START`, `LONG_HOLD`;
  - suppression для реализации комбо;
  - пример интеграции `examples/main.c`.