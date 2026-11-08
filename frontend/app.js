"use strict";

const $ = (id) => document.getElementById(id);
const board = $("board");
const uuid = /^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i;
const sideName = { white: "Белые", black: "Чёрные" };
const statusName = { waiting: "Ожидание игрока", active: "Идёт игра", finished: "Завершена" };
const errors = {
  "game not found": "Игра не найдена. Проверьте ID.",
  "the game is not waiting for a second player": "В этой игре уже нельзя занять место. Можно открыть её для просмотра.",
  "invalid player token": "Ключ игрока недействителен. В этой вкладке нельзя управлять этим игроком.",
  "the game is not active": "Игра ещё не началась или уже завершена.",
  "it is not your turn": "Сейчас ход соперника. Обновите доску.",
  "the game is already finished": "Игра уже завершена.",
  "the source cell does not contain your piece": "Выберите свою шашку.",
  "the target cell is not empty": "Клетка уже занята.",
  "illegal move": "Такой ход недопустим. Выберите подсвеченную клетку.",
  "capture is mandatory": "Взятие обязательно. Выберите шашку, которая может бить.",
  "continue capturing with the same piece": "Нужно продолжить бой той же шашкой.",
  "storage is temporarily unavailable": "Сервер временно не может обратиться к базе. Попробуйте обновить позже.",
  "the game was changed by another request, read it again and retry": "Состояние игры изменилось. Доска обновлена — выберите ход заново.",
};
let game = null;
let player = null;
let selected = null;
let busy = false;
let stale = false;
const pollInterval = 2000;
let pollDelay = pollInterval;
let pollTimer = null;
let pollRequest = null;
let pollFailed = false;

function syncStatus() {
  let text = "Автообновление включится после открытия игры.";
  if (game?.status === "finished") text = "Игра завершена. Автообновление остановлено.";
  else if (game && document.hidden) text = "Автообновление приостановлено в скрытой вкладке.";
  else if (game && pollFailed) text = `Не удалось обновить игру. Повтор через ${pollDelay / 1000} с.`;
  else if (game) text = "Автообновление: каждые 2 секунды.";
  if ($("sync-status").textContent !== text) $("sync-status").textContent = text;
}

function stopPollTimer() {
  clearTimeout(pollTimer);
  pollTimer = null;
}

function schedulePoll() {
  stopPollTimer();
  syncStatus();
  if (game && game.status !== "finished" && !busy && !pollRequest && !document.hidden) {
    pollTimer = setTimeout(pollNow, pollDelay);
  }
}

function pollNow() {
  stopPollTimer();
  if (!game || game.status === "finished" || busy || pollRequest || document.hidden) return;
  const id = game.game_id;
  pollRequest = (async () => {
    try {
      const value = await request(`/games/${id}`);
      if (game?.game_id !== id) return;
      const recovering = pollFailed;
      const changed = setGame(value);
      pollDelay = pollInterval;
      pollFailed = false;
      if (!busy && (changed || recovering)) {
        render();
        message(recovering ? "Связь с сервером восстановлена." : "Состояние игры обновлено.", "success");
      }
    } catch (error) {
      stale = true;
      selected = null;
      pollFailed = true;
      pollDelay = Math.min(pollDelay * 2, 30000);
      if (!busy) { render(); message(error.message, "error"); }
    }
  })().finally(() => { pollRequest = null; schedulePoll(); });
}

document.addEventListener("visibilitychange", () => {
  stopPollTimer();
  syncStatus();
  if (!document.hidden) pollNow();
});

// Credentials stay in this tab, never in URLs, HTML, logs or localStorage.
function storageRead(key) {
  try { return sessionStorage.getItem(key); }
  catch { $("storage-warning").hidden = false; return null; }
}

function storageWrite(key, value) {
  try { sessionStorage.setItem(key, value); }
  catch { $("storage-warning").hidden = false; }
}

function storedPlayer(id) {
  try {
    const value = JSON.parse(storageRead(`mini-checkers:player:${id}`));
    return value && /^[0-9a-f]{64}$/i.test(value.player_token) &&
      ["white", "black"].includes(value.side) ? value : null;
  } catch { return null; }
}

function message(text, tone = "") {
  $("message").textContent = text;
  $("message").className = `message ${tone}`;
}

async function request(path, method = "GET", body) {
  const headers = {};
  if (body !== undefined) headers["Content-Type"] = "application/json";
  if (player && method === "POST" && /\/(moves|finish)$/.test(path)) {
    headers["X-Player-Token"] = player.player_token;
  }
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 12000);
  try {
    const response = await fetch(`/api${path}`, {
      method, headers, body: body === undefined ? undefined : JSON.stringify(body),
      cache: "no-store", signal: controller.signal,
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(errors[data.error] || `Ошибка сервера (HTTP ${response.status}).`);
      error.status = response.status;
      throw error;
    }
    return data;
  } catch (error) {
    if (error.name === "AbortError" || error instanceof TypeError) {
      throw new Error(method === "GET" ? "Нет ответа от сервера. Автообновление повторит запрос." :
        "Нет ответа от сервера. Запрос мог выполниться: обновите игру перед повтором действия.");
    }
    throw error;
  } finally { clearTimeout(timeout); }
}

function isCell(value) {
  return Array.isArray(value) && value.length === 2 &&
    value.every((coordinate) => Number.isInteger(coordinate) && coordinate >= 0 && coordinate < 8);
}

function setGame(value) {
  if (!uuid.test(value.game_id) || !Array.isArray(value.board) || value.board.length !== 8 ||
      !value.board.every((row) => typeof row === "string" && /^[.wbWB]{8}$/.test(row)) ||
      !Object.hasOwn(statusName, value.status) || !Object.hasOwn(sideName, value.current_turn) ||
      !Number.isInteger(value.version) || value.version < 1 ||
      (value.winner !== null && !Object.hasOwn(sideName, value.winner)) ||
      (value.capture_from !== null && !isCell(value.capture_from)) ||
      !Array.isArray(value.captured_cells) || !value.captured_cells.every(isCell) ||
      !Array.isArray(value.legal_moves) || !value.legal_moves.every((move) =>
        isCell(move.from) && isCell(move.to) && (move.capture === null || isCell(move.capture)))) {
    throw new Error("Сервер вернул некорректное состояние игры.");
  }
  // Redis may briefly return an older version. Never roll back a known state.
  if (game?.game_id === value.game_id && game.version > value.version) return false;
  const changed = game?.game_id !== value.game_id || game.version !== value.version;
  const recovering = stale;
  if (game?.game_id !== value.game_id || !game.board) $("connect-id").value = value.game_id;
  game = value;
  stale = false;
  if (changed) selected = canMove() && game.capture_from ? [...game.capture_from] : null;
  storageWrite("mini-checkers:game", game.game_id);
  return changed || recovering;
}

function canMove() {
  return game && player && game.status === "active" && game.current_turn === player.side && !stale;
}

function legalTarget(row, col) {
  return selected && game.legal_moves.some((move) =>
    move.from[0] === selected[0] && move.from[1] === selected[1] &&
    move.to[0] === row && move.to[1] === col);
}

function render() {
  for (const id of ["create", "connect-id", "join", "open"]) $(id).disabled = busy;
  $("refresh").disabled = busy || !game;
  $("copy-id").disabled = busy || !game;
  $("finish").disabled = busy || !game || !player || stale || game.status === "finished";
  $("finish").textContent = game?.status === "waiting" ? "Закрыть игру" : "Сдаться";
  $("game-id").value = game?.game_id || "";
  $("player-side").textContent = player ? sideName[player.side] : (game ? "Зритель" : "—");
  $("game-status").textContent = game ? (statusName[game.status] || "Недоступно") : "Нет игры";
  $("game-version").textContent = game?.version ?? "—";

  let heading = "Доска ждёт игроков";
  let badge = "Не начата";
  if (game?.status === "waiting") { heading = "Ждём второго игрока"; badge = "Ожидание"; }
  if (game?.status === "active") {
    heading = `Ходят ${sideName[game.current_turn].toLowerCase()}`;
    badge = canMove() ? "Ваш ход" : (player ? "Ход соперника" : "Просмотр");
    if (game.capture_from) {
      heading = "Продолжение взятия";
      badge = canMove() ? "Продолжите бой" : "Идёт бой";
    }
  }
  if (game?.status === "finished") {
    heading = game.winner ? `Победили ${sideName[game.winner].toLowerCase()}` : "Игра закрыта";
    badge = "Завершена";
  }
  $("turn-status").textContent = heading;
  $("turn-badge").textContent = stale ? "Обновите доску" : badge;
  $("turn-badge").className = `badge${canMove() ? " your-turn" : ""}`;
  $("move-hint").textContent = stale ? "Состояние не подтверждено сервером. Нажмите «Обновить»." :
    canMove() && game.capture_from ? "Продолжите бой выбранной шашкой. Другую шашку выбирать нельзя." :
    selected ? "Нажмите на подсвеченную клетку, чтобы сделать ход." :
    canMove() && game.legal_moves[0]?.capture ? "Взятие обязательно. Выберите одну из бьющих шашек." :
    canMove() ? "Выберите свою шашку, затем подсвеченную клетку." :
    game?.status === "active" ? "Ожидаем ход соперника. Доска обновляется автоматически." : "Белые ходят первыми.";

  const cells = [];
  for (let row = 0; row < 8; row++) {
    for (let col = 0; col < 8; col++) {
      const piece = game?.board?.[row]?.[col] || ".";
      const selectable = canMove() && game.legal_moves.some((move) => move.from[0] === row && move.from[1] === col);
      const target = canMove() && legalTarget(row, col);
      const captured = game?.captured_cells?.some((cell) => cell[0] === row && cell[1] === col);
      const king = piece === "W" || piece === "B";
      const active = selected?.[0] === row && selected?.[1] === col;
      const cell = document.createElement("button");
      cell.type = "button";
      cell.className = `cell${(row + col) % 2 ? " dark" : ""}${active ? " selected" : ""}${target ? " legal" : ""}${captured ? " captured" : ""}`;
      cell.dataset.row = row;
      cell.dataset.col = col;
      cell.disabled = busy || !canMove() || (!selectable && !target);
      const color = piece.toLowerCase() === "w" ? "белая" : "чёрная";
      const description = piece === "." ? "пусто" : `${color} ${king ? "дамка" : "шашка"}${captured ? ", побита" : ""}`;
      cell.setAttribute("aria-label", `Строка ${row}, столбец ${col}: ${description}`);
      cell.setAttribute("aria-pressed", String(active));
      if (piece !== ".") {
        const disc = document.createElement("span");
        disc.className = `piece ${piece.toLowerCase() === "w" ? "white" : "black"}${king ? " king" : ""}`;
        if (king) disc.textContent = "Д";
        disc.setAttribute("aria-hidden", "true");
        cell.append(disc);
      }
      cells.push(cell);
    }
  }
  board.replaceChildren(...cells);
  syncStatus();
}

async function run(action) {
  if (busy) return;
  busy = true;
  stopPollTimer();
  message("Подождите…");
  render();
  try {
    // Serialize user operations with an already-running GET. Never overlap
    // polling with a move, a join, a game switch, or a manual refresh.
    if (pollRequest) await pollRequest;
    await action();
    pollDelay = pollInterval;
    pollFailed = false;
  }
  catch (error) {
    if (game && !game.board) stale = true;
    message(error.message, "error");
  }
  finally { busy = false; render(); schedulePoll(); }
}

async function refresh() {
  try { setGame(await request(`/games/${game.game_id}`)); }
  catch (error) { stale = true; throw error; }
}

function rememberPlayer(id, value) {
  player = value;
  storageWrite(`mini-checkers:player:${id}`, JSON.stringify(value));
  storageWrite("mini-checkers:game", id);
}

$("create").addEventListener("click", () => run(async () => {
  const value = await request("/games", "POST");
  rememberPlayer(value.game_id, value);
  // Keep the issued credential before the follow-up GET; never retry POST.
  game = { game_id: value.game_id };
  await refresh();
  message("Игра создана. Передайте ID второму игроку.", "success");
}));

$("connect-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const id = $("connect-id").value.trim().toLowerCase();
  if (!uuid.test(id)) { message("Введите полный ID игры в формате UUID.", "error"); return; }
  const joining = event.submitter?.id === "join";
  run(async () => {
    let credential = storedPlayer(id);
    if (joining && !credential) {
      credential = await request(`/games/${id}/join`, "POST");
      rememberPlayer(id, credential);
      game = { game_id: id };
    }
    const value = await request(`/games/${id}`);
    player = credential;
    setGame(value);
    message(joining ? "Вы в игре. Доска обновляется автоматически." : "Игра открыта.", "success");
  });
});

$("refresh").addEventListener("click", () => run(async () => {
  await refresh();
  message("Доска обновлена.", "success");
}));

board.addEventListener("click", (event) => {
  const cell = event.target.closest("button.cell");
  if (!cell || cell.disabled || !canMove()) return;
  const row = Number(cell.dataset.row), col = Number(cell.dataset.col);
  const own = game.board[row][col].toLowerCase() === (player.side === "white" ? "w" : "b");
  if (own) {
    selected = game.capture_from ? [...game.capture_from] :
      selected?.[0] === row && selected?.[1] === col ? null : [row, col];
    render();
    board.querySelector(`[data-row="${row}"][data-col="${col}"]`).focus();
  } else if (legalTarget(row, col)) {
    const from = selected;
    run(async () => {
      try { setGame(await request(`/games/${game.game_id}/moves`, "POST", { from, to: [row, col] })); }
      catch (error) {
        if (error.status === 409) await refresh();
        else { stale = true; selected = null; }
        throw error;
      }
      message(game.status === "finished" ? "Партия завершена." : game.capture_from ?
        "Взятие принято. Нужно продолжить бой той же шашкой." : "Ход принят. Теперь ход соперника.", "success");
    });
  }
});

$("finish").addEventListener("click", () => {
  const closing = game?.status === "waiting";
  if (!window.confirm(closing ? "Закрыть игру?" : "Сдаться? Победа будет присуждена сопернику.")) return;
  run(async () => {
    try { setGame(await request(`/games/${game.game_id}/finish`, "POST")); }
    catch (error) { stale = true; throw error; }
    message(closing ? "Игра закрыта." : "Вы сдались. Игра завершена.");
  });
});

$("copy-id").addEventListener("click", async () => {
  if (!game) return;
  try { await navigator.clipboard.writeText(game.game_id); message("ID скопирован.", "success"); }
  catch { $("game-id").select(); message("Скопируйте выделенный ID вручную (Ctrl+C)."); }
});

render();
const savedId = storageRead("mini-checkers:game");
if (savedId && uuid.test(savedId)) {
  run(async () => {
    player = storedPlayer(savedId);
    // A placeholder lets refresh retry after a transient startup failure.
    game = { game_id: savedId };
    await refresh();
    message("Игра восстановлена в этой вкладке.", "success");
  });
}
