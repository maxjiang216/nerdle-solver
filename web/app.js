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
    strategyHint: document.getElementById("strategy-hint"),
    guessHint: document.getElementById("guess-hint"),
    suggestion: document.getElementById("suggestion"),
    btnApply: document.getElementById("btn-apply"),
    combinedBoards: document.getElementById("combined-boards"),
    keypad: document.getElementById("keypad"),
    btnSubmit: document.getElementById("btn-submit"),
    btnReset: document.getElementById("btn-reset"),
    btnRefresh: document.getElementById("btn-refresh"),
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
        "Bellman (optimal): min expected guesses using data/optimal_policy_5.bin (copy via make micro_policy_web). " +
        "Partition: same browser engine as other lengths."
      );
    }
    if (kind === "binerdle") {
      return "Partition strategy in the browser (static data under web/data/partition/). Run make browser_partition_data after generating pools.";
    }
    if (kind === "quad") {
      return "Partition strategy in the browser. Run make browser_partition_data after generating pools.";
    }
    if (n === 10) {
      return "Partition in the browser. Maxi expects the recommended first guess unless you use a full pool artifact.";
    }
    return "Partition strategy in the browser (make browser_partition_data).";
  }

  function guessHintText() {
    return (
      "Click a tile to focus it and cycle teal → purple → black. Type digits/symbols from the keypad. " +
      "Slots that must still be green from your past guesses default to teal; others default to black. " +
      "Keys g, p, b set color and move right."
    );
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

  /**
   * For each index i, the character that is fixed at that position (any past G at i).
   * @param {number} boardIndex
   * @returns {(string|null)[]}
   */
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

  /**
   * Set G where history fixes the digit at i and the current guess matches; otherwise B,
   * except preserve manual P when the cell is still ambiguous.
   */
  function applyFeedbackDefaults() {
    for (let b = 0; b < numBoards; b++) {
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
    const cur = feedback[boardIndex][index];
    const ix = CYCLE.indexOf(cur);
    const i = ix >= 0 ? ix : 0;
    feedback[boardIndex][index] = CYCLE[(i + 1) % 3];
  }

  function setFeedbackAt(boardIndex, index, letter) {
    const u = letter.toUpperCase();
    if (u === "G" || u === "P" || u === "B") {
      feedback[boardIndex][index] = u;
      return true;
    }
    return false;
  }

  function advanceCursor() {
    cursor = (cursor + 1) % n;
  }

  function renderCombinedBoards() {
    els.combinedBoards.innerHTML = "";
    for (let b = 0; b < numBoards; b++) {
      const block = document.createElement("div");
      block.className = "board-block";
      if (numBoards > 1) {
        const lab = document.createElement("span");
        lab.className = "board-label";
        lab.textContent = `Board ${b + 1}`;
        block.appendChild(lab);
      }
      const row = document.createElement("div");
      row.className = "tile-row";
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
        t.title = "Click: cycle teal → purple → black";
        t.addEventListener("click", (ev) => {
          ev.preventDefault();
          activeBoard = b;
          cursor = i;
          cycleFeedback(b, i);
          renderCombinedBoards();
        });
        row.appendChild(t);
      }
      block.appendChild(row);
      els.combinedBoards.appendChild(block);
    }
  }

  function buildKeypad() {
    els.keypad.innerHTML = "";
    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "+", "-", "*", "/", "(", ")", "="];
    if (n === 10) {
      keys.push("²", "³");
    }
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
    bs.addEventListener("click", backspace);
    els.keypad.appendChild(bs);
    const clr = document.createElement("button");
    clr.type = "button";
    clr.textContent = "Clear";
    clr.className = "key-wide";
    clr.addEventListener("click", () => {
      guessCells = Array(n).fill("");
      cursor = 0;
      feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
      applyFeedbackDefaults();
      renderCombinedBoards();
    });
    els.keypad.appendChild(clr);
  }

  function typeKey(k) {
    if (cursor >= n) cursor = n - 1;
    guessCells[cursor] = k;
    cursor = Math.min(n - 1, cursor + 1);
    applyFeedbackDefaults();
    renderCombinedBoards();
  }

  function backspace() {
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
  }

  async function refreshEngine() {
    showError("");
    const hist = historyPayload();

    if (isMicroClassic && strategy === "bellman" && typeof window.nerdleMicroBellmanClassic === "function") {
      try {
        const data = await window.nerdleMicroBellmanClassic("", hist);
        if (data.ok) {
          applySolverResponse(data);
          return;
        }
        showError(data.error || "");
      } catch (e) {
        showError(String(e));
      }
      return;
    }

    if (typeof window.nerdleBrowserPartition === "function") {
      try {
        const data = await window.nerdleBrowserPartition("", kind, n, hist);
        if (data.ok) {
          applySolverResponse(data);
          return;
        }
        showError(data.error || "");
      } catch (e) {
        showError(String(e));
      }
      return;
    }

    showError("Browser engine not loaded (build web bundle: cd web && npm run build).");
  }

  function renderHistory() {
    els.history.innerHTML = "<strong>History</strong>";
    history.forEach((h, i) => {
      const d = document.createElement("div");
      d.className = "history-item";
      if (kind === "classic") d.textContent = `${i + 1}. ${h.guess}  → ${h.feedback}`;
      else d.textContent = `${i + 1}. ${h.guess}  →  ${h.feedbacks.join(" | ")}`;
      els.history.appendChild(d);
    });
  }

  function submitRow() {
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
    history.push(entry);
    guessCells = Array(n).fill("");
    cursor = 0;
    feedback = Array.from({ length: numBoards }, () => Array(n).fill("B"));
    applyFeedbackDefaults();
    renderCombinedBoards();
    renderHistory();
    refreshEngine();
  }

  function resetAll() {
    history = [];
    guessCells = Array(n).fill("");
    cursor = 0;
    activeBoard = 0;
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
    if (key === "g" || key === "G" || key === "p" || key === "P" || key === "b" || key === "B") {
      ev.preventDefault();
      const letter = key.toUpperCase();
      if (setFeedbackAt(activeBoard, cursor, letter)) {
        advanceCursor();
        renderCombinedBoards();
      }
      return;
    }
    if (key === "ArrowLeft") {
      ev.preventDefault();
      cursor = (cursor - 1 + n) % n;
      renderCombinedBoards();
      return;
    }
    if (key === "ArrowRight") {
      ev.preventDefault();
      advanceCursor();
      renderCombinedBoards();
      return;
    }
    if (key === "Backspace") {
      ev.preventDefault();
      backspace();
      return;
    }
    const map = {
      "0": "0",
      "1": "1",
      "2": "2",
      "3": "3",
      "4": "4",
      "5": "5",
      "6": "6",
      "7": "7",
      "8": "8",
      "9": "9",
      "+": "+",
      "-": "-",
      "*": "*",
      "/": "/",
      "(": "(",
      ")": ")",
      "=": "=",
    };
    if (map[key] !== undefined) {
      ev.preventDefault();
      typeKey(map[key]);
      return;
    }
    if (n === 10 && (key === "^" || ev.code === "Digit6")) {
      /* optional: could map ^2 - skip */
    }
  }

  /* init */
  els.title.textContent = modeTitle();
  document.title = modeTitle() + " — engine";
  els.strategyHint.textContent = strategyHintText();
  els.guessHint.textContent = guessHintText();

  if (!isMicroClassic) {
    els.strategyToggle.hidden = true;
  } else {
    els.btnBellman.addEventListener("click", () => setStrategy("bellman"));
    els.btnPartition.addEventListener("click", () => setStrategy("partition"));
    els.btnBellman.classList.toggle("active", strategy === "bellman");
    els.btnPartition.classList.toggle("active", strategy === "partition");
  }

  els.btnApply.addEventListener("click", () => {
    const s = els.suggestion.textContent.trim();
    if (!s || s === "—") return;
    guessCells = splitGuessString(s);
    while (guessCells.length < n) guessCells.push("");
    guessCells = guessCells.slice(0, n);
    cursor = Math.min(n - 1, Math.max(0, [...s].length - 1));
    applyFeedbackDefaults();
    renderCombinedBoards();
  });

  els.btnSubmit.addEventListener("click", submitRow);
  els.btnReset.addEventListener("click", resetAll);
  els.btnRefresh.addEventListener("click", () => refreshEngine());

  document.addEventListener("keydown", onKeyDown);

  buildKeypad();
  applyFeedbackDefaults();
  renderCombinedBoards();
  refreshEngine();
})();
