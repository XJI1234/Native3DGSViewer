import { WebEngine } from './engine/engine';

export type { Pose } from './engine/camera';
export { Camera } from './engine/camera';
export type { EngineOptions, LoadOperation, Snapshot } from './engine/engine';
export type { EngineError, ErrorCode, Limits, Progress, Result, Source, Vec3 } from './splat-types/index';
export { WebEngine };
export const createEngine = (options: import('./engine/engine').EngineOptions) => WebEngine.create(options);
