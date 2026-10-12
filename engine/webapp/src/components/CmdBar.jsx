import { useState } from 'react'
import { Terminal } from 'lucide-react'

export function CmdBar({ onSend }) {
  const [value, setValue] = useState('')

  const submit = () => {
    const cmd = value.trim()
    if (!cmd) return
    onSend(cmd)
    setValue('')
  }

  return (
    <div className="flex items-center gap-3 border-t-2 border-border bg-secondary px-4 py-3">
      <Terminal size={16} className="text-main shrink-0" />
      <input
        className="nb-input flex-1 px-3 py-1.5 text-sm"
        placeholder="HIDE FILE secret.txt / UNHIDE PID 0 / STATUS ..."
        value={value}
        onChange={e => setValue(e.target.value)}
        onKeyDown={e => e.key === 'Enter' && submit()}
        spellCheck={false}
        autoComplete="off"
      />
    </div>
  )
}
