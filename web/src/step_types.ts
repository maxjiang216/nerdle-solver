export type ClassicHist = { guess: string; feedback: string };
export type MultiHist = { guess: string; feedback: string[] };
export type Hist = ClassicHist | MultiHist;

export type RemainingClassic = number;
export type RemainingMulti = { boards: number[]; product: number };

export type StepOk = {
  ok: true;
  suggestion: string;
  remaining: RemainingClassic | RemainingMulti;
  solved?: boolean;
  engine: string;
};

export type StepErr = { ok: false; error: string };

export type StepResult = StepOk | StepErr;
