(function () {
  "use strict";

  const params = new URLSearchParams(window.location.search);
  const kind = params.get("kind") || "classic";
  const n = parseInt(params.get("n") || "8", 10);
  const numBoards = kind === "quad" ? 4 : kind === "binerdle" ? 2 : 1;
  const isMicroClassic = kind === "classic" && n === 5;

  const els = {
    title: document.getElementById("engine-title"),
    strategyToggle: document.getElementById("strategy-toggle"),
    btnBellman: document.getElementById("btn-bellman"),
    btnPartition: document.getElementById("btn-partition"),
    suggestion: document.getElementById("suggestion"),
    combinedBoards: document.getElementById("combined-boards"),
    keypad: document.getElementById("keypad"),
    btnSubmit: document.getElementById("btn-submit"),
    btnReset: document.getElementById("btn-reset"),
    errorMsg: document.getElementById("error-msg"),
    remaining: document.getElementById("remaining"),
    remainingNote: document.getElementById("remaining-note"),
    history: document.getElementById("history"),
  };

  /** @type {'bellman'|'partition'} */
  let strategy = isMicroClassic ? "bellman" : "partition";
  /** @type {string[]} */
  let guessCells = Array(n).fill("");
  let cursor = 0;
  /** Which board receives g/p/b when using keyboard (click a tile to focus a board). */
  let activeBoard = 0;
  /** `"board,col"` of the tile last selected by click; repeat click on that tile cycles color. */
  let lastPointerTileKey = null;
  /** @type {('G'|'P'|'B')[][]} */
  let feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
  /** @type {object[]} */
  let history = [];

  const CYCLE = ["G", "P", "B"];

  function modeTitle() {
    if (kind === "classic") {
      const names = { 5: "Micro", 6: "Mini", 7: "Midi", 8: "Normal", 10: "Maxi" };
      return `Classic — ${names[n] || n} (${n})`;
    }
    if (kind === "binerdle") return `Binerdle — ${n === 6 ? "Mini" : "Normal"} (2×${n})`;
    return "Quad Nerdle (4×8)";
  }

  function strategyHintText() {
    if (isMicroClassic) {
      return (
        "Bellman (optimal): min expected guesses. " +
        "Partition: same browser engine as other lengths."
      );
    }
    return "";
  }

  function guessHintText() {
    const nav =
      numBoards > 1
        ? "← → move the highlighted cell (wraps across boards); ↑ ↓ switch boards at the same column. "
        : "← → move the highlighted cell. ";
    return (
      nav +
      "Click a tile to select it; click again to cycle teal → purple → black. " +
      "g p b set color and move right. Backspace clears tile color; Delete erases the character. " +
      "Enter submits. Ctrl+Z (or Cmd+Z) undoes the last submit."
    );
  }

  /** Whether board b is fully solved in history (all G feedback). */
  function isBoardSolved(b) {
    if (kind === "classic") return false;
    for (const h of history) {
      const fb = h.feedbacks[b];
      if (fb && [...fb].every((c) => c === "G")) return true;
    }
    return false;
  }

  /** Answer string for board b if solved, else null. */
  function solvedAnswer(b) {
    if (kind === "classic") return null;
    for (const h of history) {
      const fb = h.feedbacks[b];
      if (fb && [...fb].every((c) => c === "G")) return h.guess;
    }
    return null;
  }

  function setStrategy(s) {
    if (!isMicroClassic) return;
    strategy = s;
    els.btnBellman.classList.toggle("active", s === "bellman");
    els.btnPartition.classList.toggle("active", s === "partition");
    refreshEngine();
  }

  function showError(msg) {
    if (!msg) {
      els.errorMsg.hidden = true;
      els.errorMsg.textContent = "";
      return;
    }
    els.errorMsg.hidden = false;
    els.errorMsg.textContent = msg;
  }

  function splitGuessString(s) {
    return [...s].slice(0, n);
  }

  function knownGreenAtPosition(boardIndex) {
    const out = Array(n).fill(null);
    for (const h of history) {
      const fb = kind === "classic" ? h.feedback : h.feedbacks[boardIndex];
      const g = [...h.guess].slice(0, n);
      for (let i = 0; i < n; i++) {
        if (fb[i] === "G") out[i] = g[i];
      }
    }
    return out;
  }

  function applyFeedbackDefaults() {
    for (let b = 0; b < numBoards; b++) {
      if (isBoardSolved(b)) continue;
      const known = knownGreenAtPosition(b);
      for (let i = 0; i < n; i++) {
        const ch = guessCells[i];
        if (known[i] != null && ch && ch === known[i]) {
          feedback[b][i] = "G";
          continue;
        }
        if (known[i] != null && ch && ch !== known[i]) {
          if (feedback[b][i] === "G") feedback[b][i] = "B";
          continue;
        }
        if (!ch) {
          feedback[b][i] = "B";
          continue;
        }
        if (feedback[b][i] === "G" && (known[i] == null || ch !== known[i])) {
          feedback[b][i] = "B";
        }
      }
    }
  }

  function cycleFeedback(boardIndex, index) {
    if (isBoardSolved(boardIndex)) return;
    const cur = feedback[boardIndex][index];
    const ix = CYCLE.indexOf(cur);
    const i = ix >= 0 ? ix : 0;
    feedback[boardIndex][index] = CYCLE[(i + 1) % 3];
  }

  function setFeedbackAt(boardIndex, index, letter) {
    if (isBoardSolved(boardIndex)) return false;
    const u = letter.toUpperCase();
    if (u === "G" || u === "P" || u === "B") {
      feedback[boardIndex][index] = u;
      return true;
    }
    return false;
  }

  function advanceCursor() {
    moveHighlight(1);
  }

  function moveHighlight(delta) {
    lastPointerTileKey = null;
    if (numBoards === 1) {
      cursor = (cursor + delta + n) % n;
      return;
    }
    const total = numBoards * n;
    let linear = activeBoard * n + cursor;
    linear = (linear + delta + total) % total;
    activeBoard = Math.floor(linear / n);
    cursor = linear % n;
  }

  function moveActiveBoard(delta) {
    if (numBoards <= 1) return;
    lastPointerTileKey = null;
    activeBoard = (activeBoard + delta + numBoards) % numBoards;
  }

  function renderCombinedBoards() {
    els.combinedBoards.innerHTML = "";
    for (let b = 0; b < numBoards; b++) {
      const solved = isBoardSolved(b);
      const answer = solvedAnswer(b);
      const block = document.createElement("div");
      block.className = "board-block";
      if (numBoards > 1) {
        const lab = document.createElement("span");
        lab.className = "board-label";
        lab.textContent = `Board ${b + 1}`;
        if (solved) {
          const tick = document.createElement("span");
          tick.className = "board-solved-tick";
          tick.textContent = " ✓";
          lab.appendChild(tick);
        }
        block.appendChild(lab);
      }
      const row = document.createElement("div");
      row.className = "tile-row";

      if (solved && answer) {
        // Show solved board: all-green locked tiles
        const chars = [...answer].slice(0, n);
        for (let i = 0; i < n; i++) {
          const t = document.createElement("div");
          t.className = "tile combined fb-green tile-solved";
          const span = document.createElement("span");
          span.className = "tile-char";
          span.textContent = chars[i] || "·";
          t.appendChild(span);
          row.appendChild(t);
        }
      } else {
        for (let i = 0; i < n; i++) {
          const t = document.createElement("div");
          const f = feedback[b][i];
          t.className =
            "tile combined fb-" +
            (f === "G" ? "green" : f === "P" ? "purple" : "black");
          if (b === activeBoard && i === cursor) t.classList.add("tile-cursor-active");
          const span = document.createElement("span");
          span.className = "tile-char";
          span.textContent = guessCells[i] || "·";
          t.appendChild(span);
          t.title = "First click selects; click again to cycle teal → purple → black";
          t.addEventListener("click", (ev) => {
            ev.preventDefault();
            const tileKey = `${b},${i}`;
            activeBoard = b;
            cursor = i;
            try {
              els.combinedBoards.focus({ preventScroll: true });
            } catch (_) {
              els.combinedBoards.focus();
            }
            if (lastPointerTileKey === tileKey) {
              cycleFeedback(b, i);
            } else {
              lastPointerTileKey = tileKey;
            }
            renderCombinedBoards();
          });
          row.appendChild(t);
        }
      }

      block.appendChild(row);
      els.combinedBoards.appendChild(block);
    }
  }

  function buildKeypad() {
    els.keypad.innerHTML = "";
    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "+", "-", "*", "/", "(", ")", "="];
    if (n === 10) keys.push("²", "³");
    keys.forEach((k) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.textContent = k;
      btn.addEventListener("click", () => typeKey(k));
      els.keypad.appendChild(btn);
    });
    const bs = document.createElement("button");
    bs.type = "button";
    bs.textContent = "\u232b";
    bs.className = "key-wide";
    bs.addEventListener("click", () => deleteDigit());
    els.keypad.appendChild(bs);
    const clr = document.createElement("button");
    clr.type = "button";
    clr.textContent = "Clear";
    clr.className = "key-wide";
    clr.addEventListener("click", () => {
      guessCells = Array(n).fill("");
      cursor = 0;
      lastPointerTileKey = null;
      feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
      applyFeedbackDefaults();
      renderCombinedBoards();
    });
    els.keypad.appendChild(clr);
  }

  function typeKey(k) {
    lastPointerTileKey = null;
    if (cursor >= n) cursor = n - 1;
    guessCells[cursor] = k;
    cursor = Math.min(n - 1, cursor + 1);
    applyFeedbackDefaults();
    renderCombinedBoards();
  }

  function backspaceFeedback() {
    lastPointerTileKey = null;
    feedback[activeBoard][cursor] = "B";
    applyFeedbackDefaults();
    renderCombinedBoards();
  }

  function deleteDigit() {
    lastPointerTileKey = null;
    if (guessCells[cursor]) {
      guessCells[cursor] = "";
    } else if (cursor > 0) {
      cursor--;
      guessCells[cursor] = "";
    }
    applyFeedbackDefaults();
    renderCombinedBoards();
  }

  function historyPayload() {
    return history.map((h) => {
      if (kind === "classic") return { guess: h.guess, feedback: h.feedback };
      return { guess: h.guess, feedback: h.feedbacks };
    });
  }

  function applySolverResponse(data) {
    if (!data.ok) {
      els.suggestion.textContent = "—";
      els.remaining.textContent = "—";
      showError(data.error || "request failed");
      return;
    }
    if (data.solved) {
      els.suggestion.textContent = data.suggestion || guessCells.join("");
      els.remaining.textContent = "Solved";
      els.remainingNote.textContent = "";
      autoApplySuggestion(data.suggestion);
      return;
    }
    els.suggestion.textContent = data.suggestion || "—";
    if (typeof data.remaining === "number") {
      els.remaining.textContent = data.remaining.toLocaleString();
      els.remainingNote.textContent = "Valid equations in pool consistent with your clues.";
    } else if (data.remaining && Array.isArray(data.remaining.boards)) {
      const br = data.remaining.boards;
      const prod = data.remaining.product != null ? data.remaining.product : data.remaining_product;
      els.remaining.textContent = br.map((x) => x.toLocaleString()).join(" · ");
      els.remainingNote.textContent =
        "Per-board counts" + (prod != null ? ` (product ${Number(prod).toLocaleString()})` : "") + ".";
    } else {
      els.remaining.textContent = "—";
      els.remainingNote.textContent = "";
    }
    autoApplySuggestion(data.suggestion);
  }

  function autoApplySuggestion(sugg) {
    if (!sugg || sugg === "—") return;
    lastPointerTileKey = null;
    guessCells = splitGuessString(sugg);
    while (guessCells.length < n) guessCells.push("");
    guessCells = guessCells.slice(0, n);
    cursor = 0;
    applyFeedbackDefaults();
    renderCombinedBoards();
  }

  async function solveHistory(hist) {
    if (isMicroClassic && strategy === "bellman" && typeof window.nerdleMicroBellmanClassic === "function") {
      try {
        const data = await window.nerdleMicroBellmanClassic("", hist);
        return data;
      } catch (e) {
        return { ok: false, error: String(e) };
      }
    }
    if (isMicroClassic && strategy === "bellman") {
      return { ok: false, error: "Micro Bellman engine not loaded." };
    }
    if (typeof window.nerdleBrowserPartition === "function") {
      try {
        return await window.nerdleBrowserPartition("", kind, n, hist);
      } catch (e) {
        return { ok: false, error: String(e) };
      }
    }
    return { ok: false, error: "Browser engine not loaded (run: cd web && npm run build)." };
  }

  async function refreshEngine() {
    showError("");
    const data = await solveHistory(historyPayload());
    if (data.ok) {
      applySolverResponse(data);
      return;
    }
    showError(data.error || "request failed");
  }

  function renderHistory() {
    els.history.innerHTML = "";
    if (history.length === 0) return;
    const header = document.createElement("div");
    header.className = "history-header";
    header.textContent = "History";
    els.history.appendChild(header);
    history.forEach((h, i) => {
      const d = document.createElement("div");
      d.className = "history-item";
      const text = document.createElement("span");
      if (kind === "classic") text.textContent = `${i + 1}. ${h.guess}  → ${h.feedback}`;
      else text.textContent = `${i + 1}. ${h.guess}  →  ${h.feedbacks.join(" | ")}`;
      d.appendChild(text);
      const del = document.createElement("button");
      del.type = "button";
      del.className = "history-del";
      del.title = "Remove this guess from history";
      del.textContent = "✕";
      del.addEventListener("click", () => deleteHistoryEntry(i));
      d.appendChild(del);
      els.history.appendChild(d);
    });
  }

  async function deleteHistoryEntry(idx) {
    history.splice(idx, 1);
    // If the removed entry was the last one, restore its guess to the grid
    // (undo-style). Otherwise just recompute.
    guessCells = Array(n).fill("");
    cursor = 0;
    lastPointerTileKey = null;
    feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
    applyFeedbackDefaults();
    renderCombinedBoards();
    renderHistory();
    showError("");
    await refreshEngine();
  }

  async function submitRow() {
    showError("");
    if (guessCells.some((c) => !c)) {
      showError(`Fill all ${n} guess tiles.`);
      return;
    }
    const g = guessCells.join("");
    const entry =
      kind === "classic"
        ? { guess: g, feedback: feedback[0].join("") }
        : { guess: g, feedbacks: feedback.map((row) => row.join("")) };
    const proposedHistory = historyPayload().concat(
      kind === "classic"
        ? { guess: entry.guess, feedback: entry.feedback }
        : { guess: entry.guess, feedback: entry.feedbacks }
    );
    const data = await solveHistory(proposedHistory);
    if (!data.ok) {
      showError(data.error || "No candidates remain — check guess and feedback.");
      return;
    }
    history.push(entry);
    lastPointerTileKey = null;
    feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
    renderHistory();
    applySolverResponse(data);
  }

  /** Undo: pop last history entry and restore its guess + feedback to the grid. */
  async function undoSubmit() {
    if (history.length === 0) return;
    const last = history.pop();
    lastPointerTileKey = null;
    guessCells = splitGuessString(last.guess);
    while (guessCells.length < n) guessCells.push("");
    if (kind === "classic") {
      feedback = [last.feedback.split("")];
    } else {
      feedback = last.feedbacks.map((f) => f.split(""));
    }
    cursor = 0;
    renderCombinedBoards();
    renderHistory();
    showError("");
    const data = await solveHistory(historyPayload());
    if (data.ok) {
      // show suggestion but don't auto-apply (user is editing the undone row)
      els.suggestion.textContent = data.suggestion || "—";
      if (typeof data.remaining === "number") {
        els.remaining.textContent = data.remaining.toLocaleString();
        els.remainingNote.textContent = "Valid equations in pool consistent with your clues.";
      } else if (data.remaining && Array.isArray(data.remaining.boards)) {
        const br = data.remaining.boards;
        const prod = data.remaining.product != null ? data.remaining.product : data.remaining_product;
        els.remaining.textContent = br.map((x) => x.toLocaleString()).join(" · ");
        els.remainingNote.textContent =
          "Per-board counts" + (prod != null ? ` (product ${Number(prod).toLocaleString()})` : "") + ".";
      }
    }
  }

  function resetAll() {
    history = [];
    guessCells = Array(n).fill("");
    cursor = 0;
    activeBoard = 0;
    lastPointerTileKey = null;
    feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
    applyFeedbackDefaults();
    renderCombinedBoards();
    renderHistory();
    showError("");
    refreshEngine();
  }

  function onKeyDown(ev) {
    const t = ev.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable)) return;

    const key = ev.key;

    // Ctrl/Cmd+Z = undo
    if (key === "z" && (ev.ctrlKey || ev.metaKey)) {
      ev.preventDefault();
      undoSubmit();
      return;
    }

    if (key === "Enter") {
      ev.preventDefault();
      submitRow();
      return;
    }

    if (key === "g" || key === "G" || key === "p" || key === "P" || key === "b" || key === "B") {
      // Don't intercept if a modifier is held (browser shortcuts)
      if (!ev.ctrlKey && !ev.metaKey && !ev.altKey) {
        ev.preventDefault();
        const letter = key.toUpperCase();
        if (setFeedbackAt(activeBoard, cursor, letter)) {
          advanceCursor();
          renderCombinedBoards();
        }
        return;
      }
    }
    if (key === "ArrowLeft") {
      ev.preventDefault();
      moveHighlight(-1);
      renderCombinedBoards();
      return;
    }
    if (key === "ArrowRight") {
      ev.preventDefault();
      moveHighlight(1);
      renderCombinedBoards();
      return;
    }
    if (key === "ArrowUp") {
      ev.preventDefault();
      moveActiveBoard(-1);
      renderCombinedBoards();
      return;
    }
    if (key === "ArrowDown") {
      ev.preventDefault();
      moveActiveBoard(1);
      renderCombinedBoards();
      return;
    }
    if (key === "Backspace") {
      ev.preventDefault();
      backspaceFeedback();
      return;
    }
    if (key === "Delete") {
      ev.preventDefault();
      deleteDigit();
      return;
    }
    const map = {
      "0": "0", "1": "1", "2": "2", "3": "3", "4": "4",
      "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
      "+": "+", "-": "-", "*": "*", "/": "/",
      "(": "(", ")": ")", "=": "=",
    };
    if (map[key] !== undefined) {
      ev.preventDefault();
      typeKey(map[key]);
      return;
    }
  }

  /* ── init ── */
  els.combinedBoards.tabIndex = 0;
  els.combinedBoards.setAttribute("role", "grid");
  els.combinedBoards.setAttribute("aria-label", "Current guess and per-tile feedback");

  els.title.textContent = modeTitle();
  document.title = modeTitle() + " — Nerdle solver";

  // Strategy toggle: only show for Micro (Bellman vs Partition)
  els.strategyToggle.hidden = !isMicroClassic;
  els.btnBellman.hidden = !isMicroClassic;
  if (isMicroClassic) {
    const hint = document.getElementById("strategy-hint");
    if (hint) hint.textContent = strategyHintText();
    els.btnBellman.addEventListener("click", () => setStrategy("bellman"));
    els.btnPartition.addEventListener("click", () => setStrategy("partition"));
    els.btnBellman.classList.toggle("active", strategy === "bellman");
    els.btnPartition.classList.toggle("active", strategy === "partition");
  } else {
    const hint = document.getElementById("strategy-hint");
    if (hint) hint.hidden = true;
  }

  const guessHintEl = document.getElementById("guess-hint");
  if (guessHintEl) guessHintEl.textContent = guessHintText();

  els.btnSubmit.addEventListener("click", submitRow);
  els.btnReset.addEventListener("click", resetAll);

  document.addEventListener("keydown", onKeyDown);

  buildKeypad();
  applyFeedbackDefaults();
  renderCombinedBoards();
  refreshEngine();
})();
