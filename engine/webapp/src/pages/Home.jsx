import { useState } from 'react'
import { Monitor, CheckCircle, EyeOff, Shield, Search, LayoutGrid, List, RefreshCw, ChevronRight } from 'lucide-react'
import { StatCard } from '../components/StatCard'
import { Badge } from '../components/Badge'

export function Home({ targets, onNavigate, onRefresh }) {
  const [search, setSearch] = useState('')
  const [view, setView] = useState('grid')

  const online = targets.filter(t => t.online).length
  const hidden = targets.filter(t => t.module_hidden).length
  const totalHidden = targets.reduce((s, t) => {
    const c = t.counts || {}
    return s + (c.files || 0) + (c.pids || 0) + (c.content || 0) + (c.ports || 0)
  }, 0)

  const q = search.toLowerCase()
  const filtered = q
    ? targets.filter(t =>
        (t.hostname || '').toLowerCase().includes(q) ||
        (t.id || '').toLowerCase().includes(q) ||
        (t.exercise || '').toLowerCase().includes(q)
      )
    : targets

  return (
    <div className="p-6 space-y-6">
      <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
        <StatCard label="Total Targets" value={targets.length} color="blue" icon={Monitor} />
        <StatCard label="Online" value={online} color="green" icon={CheckCircle} />
        <StatCard label="Module Hidden" value={hidden} color="purple" icon={EyeOff} />
        <StatCard label="Hidden Items" value={totalHidden} color="amber" icon={Shield} />
      </div>

      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
        <div>
          <h2 className="text-xl font-bold">Connected Targets</h2>
          <p className="text-sm text-fg-3">{filtered.length} of {targets.length} target{targets.length !== 1 ? 's' : ''}</p>
        </div>
        <div className="flex items-center gap-3">
          <div className="relative">
            <Search size={14} className="absolute left-2.5 top-1/2 -translate-y-1/2 text-fg-3" />
            <input
              className="nb-input pl-8 pr-3 py-1.5 text-sm w-52"
              placeholder="Search targets..."
              value={search}
              onChange={e => setSearch(e.target.value)}
            />
          </div>
          <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden">
            <button
              className={`px-2.5 py-1.5 text-sm font-bold flex items-center gap-1.5 transition-colors
                ${view === 'grid' ? 'bg-main text-main-fg' : 'bg-secondary hover:bg-bg-2'}`}
              onClick={() => setView('grid')}
            >
              <LayoutGrid size={14} /> Grid
            </button>
            <button
              className={`px-2.5 py-1.5 text-sm font-bold flex items-center gap-1.5 border-l-2 border-border transition-colors
                ${view === 'table' ? 'bg-main text-main-fg' : 'bg-secondary hover:bg-bg-2'}`}
              onClick={() => setView('table')}
            >
              <List size={14} /> Table
            </button>
          </div>
          <button className="nb-btn p-2" onClick={onRefresh} title="Refresh">
            <RefreshCw size={14} />
          </button>
        </div>
      </div>

      {!filtered.length ? (
        <div className="nb-card p-12 text-center">
          <Monitor size={40} className="mx-auto text-fg-3 mb-3" />
          <p className="font-bold text-fg-2">{targets.length ? 'No matching targets' : 'No targets connected'}</p>
          <p className="text-sm text-fg-3 font-mono mt-1">
            {targets.length ? 'Try a different search term' : './rklings run EXERCISE --reference'}
          </p>
        </div>
      ) : view === 'grid' ? (
        <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4">
          {filtered.map(t => <TargetCard key={t.id} target={t} onClick={() => onNavigate(t.id)} />)}
        </div>
      ) : (
        <TargetTable targets={filtered} onNavigate={onNavigate} />
      )}
    </div>
  )
}

function TargetCard({ target: t, onClick }) {
  const total = (t.counts?.files || 0) + (t.counts?.pids || 0) + (t.counts?.content || 0) + (t.counts?.ports || 0)
  const barColor = t.online ? (t.module_hidden ? 'bg-green' : 'bg-blue') : 'bg-red'

  return (
    <button className="nb-card text-left w-full group" onClick={onClick}>
      <div className={`h-1.5 ${barColor} rounded-t-[3px]`} />
      <div className="p-4 space-y-3">
        <div className="flex items-start justify-between">
          <div>
            <div className="font-bold text-lg">{(t.hostname && t.hostname !== '(none)') ? t.hostname : t.id}</div>
            <div className="text-xs font-mono text-fg-3">{t.id}</div>
          </div>
          <Badge label={t.online ? 'Online' : 'Offline'} color={t.online ? 'green' : 'red'} />
        </div>

        <div className="grid grid-cols-2 gap-2 text-sm">
          <Field label="Kernel" value={`${t.kernel || '--'} ${t.arch || ''}`} />
          <Field label="Module" value={t.module_hidden ? 'Hidden' : 'Visible'} color={t.module_hidden ? 'text-green' : 'text-fg-3'} />
          <Field label="Hidden" value={total ? `${total} items` : 'None'} color={total ? 'text-amber' : 'text-fg-3'} />
          {t.peer && <Field label="Peer" value={t.peer} />}
        </div>

        <div className="flex items-center justify-between pt-2 border-t-2 border-border text-sm">
          <span className={`font-bold flex items-center gap-1.5 ${t.online ? 'text-green' : 'text-red'}`}>
            <span className={`w-2 h-2 rounded-full ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
            {t.online ? 'Online' : 'Offline'}
          </span>
          <ChevronRight size={14} className="text-fg-3 opacity-0 group-hover:opacity-100 transition-opacity" />
        </div>
      </div>
    </button>
  )
}

function Field({ label, value, color }) {
  return (
    <div>
      <div className="text-[10px] font-bold uppercase tracking-widest text-fg-3">{label}</div>
      <div className={`font-mono text-sm ${color || ''}`}>{value}</div>
    </div>
  )
}

function TargetTable({ targets, onNavigate }) {
  return (
    <div className="nb-card-flat overflow-hidden">
      <table className="w-full text-sm">
        <thead>
          <tr className="border-b-2 border-border bg-bg-2">
            <Th>Status</Th><Th>Target</Th><Th>Peer</Th><Th>Kernel</Th><Th>Module</Th><Th>Hidden</Th><th className="w-8" />
          </tr>
        </thead>
        <tbody>
          {targets.map(t => {
            const total = (t.counts?.files || 0) + (t.counts?.pids || 0) + (t.counts?.content || 0) + (t.counts?.ports || 0)
            return (
              <tr key={t.id} className="border-b-2 border-border hover:bg-bg-2 cursor-pointer transition-colors" onClick={() => onNavigate(t.id)}>
                <td className="px-4 py-2.5"><Badge label={t.online ? 'Online' : 'Offline'} color={t.online ? 'green' : 'red'} /></td>
                <td className="px-4 py-2.5">
                  <div className="font-bold">{(t.hostname && t.hostname !== '(none)') ? t.hostname : t.id}</div>
                  <div className="text-xs font-mono text-fg-3">{t.id}</div>
                </td>
                <td className="px-4 py-2.5 font-mono text-xs text-fg-3">{t.peer || '-'}</td>
                <td className="px-4 py-2.5 font-mono text-xs">{t.kernel || '--'} {t.arch || ''}</td>
                <td className="px-4 py-2.5">{t.module_hidden ? <span className="font-bold text-green">Hidden</span> : <span className="text-fg-3">Visible</span>}</td>
                <td className="px-4 py-2.5">{total ? <span className="font-bold text-amber">{total}</span> : <span className="text-fg-3">0</span>}</td>
                <td className="px-4 py-2.5 text-fg-3"><ChevronRight size={14} /></td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}

function Th({ children }) {
  return <th className="text-left px-4 py-2.5 font-bold uppercase text-xs tracking-wide">{children}</th>
}
