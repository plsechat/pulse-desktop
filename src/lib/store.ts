import { app } from 'electron';
import fs from 'fs';
import path from 'path';

export interface WindowState {
  x?: number;
  y?: number;
  width: number;
  height: number;
  isMaximized: boolean;
}

export interface Settings {
  serverUrl?: string;
  serverName?: string;
  minimizeToTray: boolean;
  windowState?: WindowState;
}

const DEFAULT_SETTINGS: Settings = {
  minimizeToTray: false,
};

export class Store {
  private filePath: string;
  private data: Settings;

  constructor() {
    this.filePath = path.join(app.getPath('userData'), 'settings.json');
    this.data = this.load();
  }

  private load(): Settings {
    try {
      const raw = fs.readFileSync(this.filePath, 'utf-8');
      return { ...DEFAULT_SETTINGS, ...JSON.parse(raw) };
    } catch {
      return { ...DEFAULT_SETTINGS };
    }
  }

  private save(): void {
    const dir = path.dirname(this.filePath);
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }
    fs.writeFileSync(this.filePath, JSON.stringify(this.data, null, 2), 'utf-8');
  }

  get<K extends keyof Settings>(key: K): Settings[K] {
    return this.data[key];
  }

  set<K extends keyof Settings>(key: K, value: Settings[K]): void {
    this.data[key] = value;
    this.save();
  }

  delete<K extends keyof Settings>(key: K): void {
    delete this.data[key];
    this.save();
  }

  getAll(): Settings {
    return { ...this.data };
  }
}
