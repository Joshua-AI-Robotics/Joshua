type PickerAcceptType = {
  description: string;
  accept: Record<string, string[]>;
};

type SavePickerOptions = {
  suggestedName?: string;
  types?: PickerAcceptType[];
  excludeAcceptAllOption?: boolean;
};

type OpenPickerOptions = {
  multiple?: boolean;
  types?: PickerAcceptType[];
  excludeAcceptAllOption?: boolean;
};

type WritableFileStream = {
  write: (data: string | Blob) => Promise<void>;
  close: () => Promise<void>;
  abort?: () => Promise<void>;
};

type WritableFileHandle = {
  createWritable: () => Promise<WritableFileStream>;
};

type ReadableFileHandle = {
  getFile: () => Promise<File>;
};

type FilePickerWindow = Window & {
  showSaveFilePicker?: (options?: SavePickerOptions) => Promise<WritableFileHandle>;
  showOpenFilePicker?: (options?: OpenPickerOptions) => Promise<ReadableFileHandle[]>;
};

type TextSource = string | (() => string | Promise<string>);

export type FileSaveResult = 'saved' | 'downloaded' | 'cancelled';

export type TextFileOpenResult = {
  name: string;
  text: string;
};

type SaveTextFileOptions = {
  suggestedName: string;
  text: TextSource;
  mimeType: string;
  types: PickerAcceptType[];
};

type OpenTextFileOptions = {
  types: PickerAcceptType[];
};

function filePickerWindow(): FilePickerWindow {
  return window as FilePickerWindow;
}

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

function pickerAccept(types: PickerAcceptType[]): string {
  const accepted = new Set<string>();
  for (const type of types) {
    for (const mimeType of Object.keys(type.accept)) accepted.add(mimeType);
    for (const extensions of Object.values(type.accept)) {
      for (const extension of extensions) accepted.add(extension);
    }
  }
  return Array.from(accepted).join(',');
}

async function resolveText(source: TextSource): Promise<string> {
  return typeof source === 'function' ? source() : source;
}

function downloadTextFile(text: string, filename: string, mimeType: string): void {
  const blob = new Blob([text], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export async function saveTextFile({
  suggestedName,
  text,
  mimeType,
  types,
}: SaveTextFileOptions): Promise<FileSaveResult> {
  const savePicker = filePickerWindow().showSaveFilePicker;

  if (savePicker) {
    try {
      const handle = await savePicker.call(window, {
        suggestedName,
        types,
      });
      const resolvedText = await resolveText(text);
      const writable = await handle.createWritable();
      try {
        await writable.write(resolvedText);
        await writable.close();
      } catch (error) {
        await writable.abort?.();
        throw error;
      }
      return 'saved';
    } catch (error) {
      if (isAbortError(error)) return 'cancelled';
      throw error;
    }
  }

  downloadTextFile(await resolveText(text), suggestedName, mimeType);
  return 'downloaded';
}

export async function openTextFile({
  types,
}: OpenTextFileOptions): Promise<TextFileOpenResult | null> {
  const openPicker = filePickerWindow().showOpenFilePicker;

  if (openPicker) {
    try {
      const handles = await openPicker.call(window, {
        multiple: false,
        types,
      });
      const file = await handles[0]?.getFile();
      return file ? { name: file.name, text: await file.text() } : null;
    } catch (error) {
      if (isAbortError(error)) return null;
      throw error;
    }
  }

  return new Promise((resolve, reject) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = pickerAccept(types);
    input.style.display = 'none';

    const finish = (result: TextFileOpenResult | null) => {
      input.remove();
      resolve(result);
    };

    input.addEventListener(
      'change',
      async () => {
        const file = input.files?.[0];
        if (!file) {
          finish(null);
          return;
        }
        try {
          finish({ name: file.name, text: await file.text() });
        } catch (error) {
          input.remove();
          reject(error);
        }
      },
      { once: true },
    );

    input.addEventListener('cancel', () => finish(null), { once: true });
    document.body.append(input);
    input.click();
  });
}
