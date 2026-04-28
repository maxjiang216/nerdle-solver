import { browserPartitionStep } from "./browser_engine";
import { computeFeedbackPacked, feedbackPackedToString, normalizeGuessInput } from "./feedback";
import { microBellmanClassicStep } from "./micro_bellman";

export type { Hist, StepResult } from "./step_types";

declare global {
  interface Window {
    nerdleBrowserPartition: typeof browserPartitionStep;
    nerdleMicroBellmanClassic: typeof microBellmanClassicStep;
    nerdleGuessFeedback: (guess: string, solution: string, n: number) => string;
    nerdleGuessesEquivalent: (a: string, b: string, n: number) => boolean;
  }
}

window.nerdleBrowserPartition = browserPartitionStep;
window.nerdleMicroBellmanClassic = microBellmanClassicStep;

window.nerdleGuessFeedback = (guess: string, solution: string, n: number) =>
  feedbackPackedToString(computeFeedbackPacked(guess, solution, n), n);

window.nerdleGuessesEquivalent = (a: string, b: string, n: number) =>
  normalizeGuessInput(a, n === 10) === normalizeGuessInput(b, n === 10);
