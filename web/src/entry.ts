import { browserPartitionStep } from "./browser_engine";
import { microBellmanClassicStep } from "./micro_bellman";

export type { Hist, StepResult } from "./step_types";

declare global {
  interface Window {
    nerdleBrowserPartition: typeof browserPartitionStep;
    nerdleMicroBellmanClassic: typeof microBellmanClassicStep;
  }
}

window.nerdleBrowserPartition = browserPartitionStep;
window.nerdleMicroBellmanClassic = microBellmanClassicStep;
