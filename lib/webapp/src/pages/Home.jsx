import { Monitor, ChevronRight, Shield, Network, Radio, SquareTerminal, FolderOpen, Activity, Wifi, Eye } from 'lucide-react'
import { API_HOST } from '../api'
import { useState, useEffect, useRef, useCallback } from 'react'

export function Home({ targets, onNavigate }) {
  const online = targets.filter(t => t.online).length

  return (
    <div className="p-6 max-w-5xl mx-auto space-y-5">
      <NetworkMap targets={targets} onNavigate={onNavigate} />

      <div className="flex items-baseline justify-between">
        <h2 className="text-lg font-bold">Targets</h2>
        <span className="text-xs font-mono text-fg-3 tabular-nums">{online}/{targets.length} online</span>
      </div>

      {!targets.length ? (
        <div className="nb-card p-12 text-center">
          <Monitor size={32} className="mx-auto text-fg-3 mb-3" />
          <p className="font-bold text-fg-2">No targets connected</p>
          <p className="text-sm text-fg-3 font-mono mt-1">Waiting for agents to call back...</p>
        </div>
      ) : (
        <div className="space-y-3">
          {targets.map(t => <TargetCard key={t.id} target={t} onClick={() => onNavigate(t.id)} />)}
        </div>
      )}
    </div>
  )
}

function ContextMenu({ x, y, target, onNavigate, onClose }) {
  const ref = useRef()

  useEffect(() => {
    const handler = (e) => { if (ref.current && !ref.current.contains(e.target)) onClose() }
    document.addEventListener('mousedown', handler)
    return () => document.removeEventListener('mousedown', handler)
  }, [onClose])

  const items = [
    { icon: Eye, label: 'Overview', tab: 'overview' },
    { icon: SquareTerminal, label: 'Shell', tab: 'shell' },
    { icon: FolderOpen, label: 'Files', tab: 'files' },
    { icon: Activity, label: 'Processes', tab: 'procs' },
    { icon: Wifi, label: 'Ports', tab: 'ports' },
  ]

  const name = target.label || (target.hostname && target.hostname !== '(none)' ? target.hostname : target.id)

  return (
    <div ref={ref} className="fixed z-50 border-2 border-border rounded-[var(--radius)] bg-secondary shadow-[4px_4px_0px_0px_var(--color-border)] min-w-[180px] py-1"
      style={{ left: x, top: y }}>
      <div className="px-3 py-1.5 border-b-2 border-border flex items-center gap-2">
        <span className={`w-2 h-2 rounded-full shrink-0 ${target.online ? 'bg-green pulse' : 'bg-red'}`} />
        <span className="text-xs font-bold truncate">{name}</span>
      </div>
      {items.map(({ icon: Icon, label, tab }) => (
        <button key={tab}
          className="w-full px-3 py-1.5 text-left text-sm flex items-center gap-2.5 hover:bg-main hover:text-main-fg transition-colors"
          onClick={() => { onNavigate(target.id, tab); onClose() }}>
          <Icon size={14} />
          {label}
        </button>
      ))}
    </div>
  )
}

function NetworkMap({ targets, onNavigate }) {
  const hasTargets = targets.length > 0
  const [ctx, setCtx] = useState(null)

  const onCtx = useCallback((e, t) => {
    e.preventDefault()
    setCtx({ x: e.clientX, y: e.clientY, target: t })
  }, [])

  return (
    <div className="nb-card overflow-hidden">
      <div className="px-4 py-2 border-b-2 border-border bg-bg-2 flex items-center gap-2">
        <Network size={14} className="text-fg-3" />
        <span className="text-sm font-bold uppercase tracking-wide">Network</span>
        <span className="text-[10px] font-mono text-fg-3 ml-auto tabular-nums">{API_HOST}</span>
      </div>

      <div className="bg-bg px-6 py-8" style={{
        backgroundImage: 'radial-gradient(circle, var(--color-border) 0.8px, transparent 0.8px)',
        backgroundSize: '16px 16px',
      }}>
        <div className="flex flex-col items-center">
          {/* C2 operator node */}
          <div className="border-2 border-border rounded-[var(--radius)] bg-main px-5 py-2.5 shadow-[4px_4px_0px_0px_var(--color-border)] flex items-center gap-2">
            <Radio size={14} className="text-main-fg" />
            <span className="text-sm font-bold text-main-fg uppercase tracking-wider">C2</span>
            <span className="text-xs font-mono text-main-fg/70">{API_HOST}</span>
          </div>

          {/* trunk */}
          {hasTargets && <div className="w-0.5 bg-border" style={{ height: 28 }} />}

          {/* horizontal branch for multiple targets */}
          {hasTargets && targets.length > 1 && (
            <div className="relative w-full flex justify-center" style={{ height: 0 }}>
              <div className="h-0.5 bg-border absolute top-0" style={{
                width: `calc(${((targets.length - 1) / targets.length) * 100}% - 24px)`,
              }} />
            </div>
          )}

          {/* target nodes */}
          {hasTargets ? (
            <div className="flex flex-wrap justify-center gap-4"
              style={targets.length > 1 ? { display: 'grid', gridTemplateColumns: `repeat(${Math.min(targets.length, 4)}, auto)`, gap: '16px', justifyContent: 'center' } : undefined}>
              {targets.map(t => {
                const name = t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)
                return (
                  <div key={t.id} className="flex flex-col items-center">
                    {targets.length > 1 && <>
                      <div className={`w-0.5 ${t.online ? 'bg-green' : 'bg-red'}`} style={{ height: 20 }} />
                      <svg width="10" height="6" viewBox="0 0 10 6" className="shrink-0 -mt-px mb-1">
                        <polygon points="0,0 10,0 5,6" fill={t.online ? 'var(--color-green)' : 'var(--color-red)'} />
                      </svg>
                    </>}
                    <button
                      className={`border-2 rounded-[var(--radius)] bg-bg shadow-[2px_2px_0px_0px_var(--color-border)] cursor-pointer
                        transition-all duration-150 hover:translate-y-[-2px] hover:shadow-[2px_4px_0px_0px_var(--color-border)]
                        active:translate-y-[1px] active:shadow-[0px_0px_0px_0px_var(--color-border)]
                        ${t.online ? 'border-green' : 'border-red border-dashed opacity-70'}`}
                      onClick={() => onNavigate(t.id)}
                      onContextMenu={(e) => onCtx(e, t)}
                    >
                      <div className="flex items-center gap-2.5 px-3 py-2">
                        <PlatformIcon kernel={t.kernel} size={24} />
                        <div className="min-w-0">
                          <div className="flex items-center gap-1.5">
                            <span className={`w-1.5 h-1.5 rounded-full shrink-0 ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
                            <span className="font-bold text-xs truncate">{name}</span>
                          </div>
                          <div className="text-[10px] font-mono text-fg-3 truncate">{t.peer || t.id}</div>
                        </div>
                        {(t.module_hidden || t.persisted) && (
                          <div className="flex flex-col gap-0.5 shrink-0">
                            {t.module_hidden && <span className="text-[8px] font-bold bg-green/15 text-green px-1 py-px rounded uppercase">hid</span>}
                            {t.persisted && <span className="text-[8px] font-bold bg-main/15 text-main px-1 py-px rounded uppercase">per</span>}
                          </div>
                        )}
                      </div>
                    </button>
                  </div>
                )
              })}
            </div>
          ) : (
            <div className="text-center py-6 mt-4">
              <Monitor size={28} className="mx-auto text-fg-3 mb-2" />
              <div className="text-sm text-fg-3 font-mono">Waiting for callbacks...</div>
            </div>
          )}
        </div>
      </div>

      {ctx && <ContextMenu x={ctx.x} y={ctx.y} target={ctx.target} onNavigate={onNavigate} onClose={() => setCtx(null)} />}
    </div>
  )
}

function TuxIcon({ size = 48 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg" fill="none">
      <path fill="#202020" d="M13.338 12.033c-.1-.112-.146-.319-.197-.54-.05-.22-.107-.457-.288-.61v-.001a.756.756 0 00-.223-.134c.252-.745.153-1.487-.1-2.157-.312-.823-.855-1.54-1.27-2.03-.464-.586-.918-1.142-.91-1.963.014-1.254.138-3.579-2.068-3.582-.09 0-.183.004-.28.012-2.466.198-1.812 2.803-1.849 3.675-.045.638-.174 1.14-.613 1.764-.515.613-1.24 1.604-1.584 2.637-.162.487-.24.984-.168 1.454-.023.02-.044.041-.064.063-.151.161-.263.357-.388.489-.116.116-.282.16-.464.225-.183.066-.383.162-.504.395v.001a.702.702 0 00-.077.339c0 .108.016.217.032.322.034.22.068.427.023.567-.144.395-.163.667-.061.865.102.199.31.286.547.335.473.1 1.114.075 1.619.342l.043-.082-.043.082c.54.283 1.089.383 1.526.284a.99.99 0 00.706-.552c.342-.002.717-.146 1.318-.18.408-.032.918.145 1.503.113a.806.806 0 00.068.183l.001.001c.227.455.65.662 1.1.627.45-.036.928-.301 1.315-.762l-.07-.06.07.06c.37-.448.982-.633 1.388-.878.203-.123.368-.276.38-.499.013-.222-.118-.471-.418-.805z"/>
      <path fill="#F8BF11" d="M13.571 12.828c-.007.137-.107.24-.29.35-.368.222-1.019.414-1.434.918-.362.43-.802.665-1.19.696-.387.03-.721-.13-.919-.526v-.002c-.123-.233-.072-.6.031-.987s.251-.785.271-1.108v-.001c.02-.415.044-.776.114-1.055.07-.28.179-.468.373-.575a.876.876 0 01.027-.014c.022.359.2.725.514.804.343.09.838-.204 1.047-.445l.122-.004c.184-.005.337.006.495.143v.001c.121.102.179.296.229.512.05.217.09.453.239.621.287.32.38.534.371.672zM6.592 13.843v.003c-.034.435-.28.672-.656.758-.377.086-.888 0-1.398-.266-.565-.3-1.237-.27-1.667-.36-.216-.045-.357-.113-.421-.238-.064-.126-.066-.345.071-.72v-.001l.001-.002c.068-.209.018-.438-.015-.653-.033-.214-.049-.41.024-.546l.001-.001c.094-.181.232-.246.403-.307.17-.062.373-.11.533-.27l.001-.001h.001c.148-.157.26-.353.39-.492.11-.117.22-.195.385-.196h.005a.61.61 0 01.093.008c.22.033.411.187.596.437l.533.971v.001c.142.296.441.622.695.954.254.333.45.666.425.921z"/>
      <path fill="#D6A312" d="M9.25 4.788c-.043-.084-.13-.164-.28-.225-.31-.133-.444-.142-.617-.254-.28-.181-.513-.244-.706-.244a.834.834 0 00-.272.047c-.236.08-.392.25-.49.342-.02.019-.044.035-.104.08-.06.043-.15.11-.28.208-.117.086-.154.2-.114.332.04.132.167.285.4.417h.001c.145.085.244.2.358.291a.801.801 0 00.189.117c.072.031.156.052.26.058.248.015.43-.06.59-.151.16-.092.296-.204.452-.255h.001c.32-.1.548-.301.62-.493a.324.324 0 00-.008-.27z"/>
      <path fill="#202020" d="M8.438 5.26c-.255.133-.552.294-.869.294-.316 0-.566-.146-.745-.289-.09-.07-.163-.142-.218-.193-.096-.075-.084-.181-.045-.178.066.008.076.095.117.134.056.052.126.12.211.187.17.135.397.266.68.266.284 0 .614-.166.816-.28.115-.064.26-.179.379-.266.09-.067.087-.147.162-.138.075.009.02.089-.085.18-.105.092-.27.214-.403.283z"/>
      <path fill="#ffffff" d="M12.337 10.694a1.724 1.724 0 00-.104 0h-.01c.088-.277-.106-.48-.621-.713-.534-.235-.96-.212-1.032.265-.005.025-.009.05-.011.076a.801.801 0 00-.12.054c-.252.137-.389.386-.465.692-.076.305-.098.674-.119 1.09-.013.208-.099.49-.186.79-.875.624-2.09.894-3.122.19-.07-.11-.15-.22-.233-.328a13.85 13.85 0 00-.16-.205.65.65 0 00.268-.05.34.34 0 00.186-.192c.063-.17 0-.408-.202-.68-.201-.273-.542-.58-1.043-.888-.368-.23-.574-.51-.67-.814-.097-.305-.084-.635-.01-.96.143-.625.51-1.233.743-1.614.063-.046.023.086-.236.567-.232.44-.667 1.455-.072 2.248.016-.564.15-1.14.377-1.677.329-.747 1.018-2.041 1.072-3.073.029.02.125.086.169.11.126.075.221.184.344.283a.85.85 0 00.575.2c.24 0 .427-.079.582-.168.17-.096.304-.204.433-.245.27-.085.486-.235.608-.41.21.83.7 2.027 1.014 2.611.167.31.5.969.643 1.762.091-.002.191.01.299.038.375-.973-.319-2.022-.636-2.314-.128-.124-.135-.18-.07-.177.343.304.795.917.96 1.608.075.315.09.646.01.973.04.017.08.034.12.054.603.293.826.548.719.897z"/>
      <path fill="#E6E6E6" d="M8.04 8.062c-.556.002-1.099.251-1.558.716-.46.464-.814 1.122-1.018 1.888l.061.038v.004c.47.298.805.598 1.012.878.219.296.316.584.223.834a.513.513 0 01-.27.283l-.041.015c.074.097.146.197.213.3.944.628 2.042.396 2.867-.172.08-.278.153-.536.163-.698.021-.415.042-.792.124-1.12.082-.33.242-.63.544-.795.017-.01.034-.015.051-.023a.756.756 0 01.022-.094c-.242-.622-.591-1.14-1.01-1.5-.42-.36-.897-.551-1.382-.554zm2.37 2.155l-.002.005v-.002l.001-.004z"/>
      <path fill="#ffffff" d="M9.278 3.833a1.05 1.05 0 01-.215.656 4.119 4.119 0 00-.218-.09l-.127-.045c.029-.035.085-.075.107-.127a.669.669 0 00.05-.243l.001-.01a.673.673 0 00-.035-.236.434.434 0 00-.108-.184.223.223 0 00-.156-.07H8.57a.228.228 0 00-.151.06.434.434 0 00-.122.175.676.676 0 00-.05.243v.01a.718.718 0 00.009.14 1.773 1.773 0 00-.354-.12 1.196 1.196 0 01-.01-.133v-.013a1.035 1.035 0 01.088-.447.793.793 0 01.25-.328.554.554 0 01.346-.123h.006c.125 0 .232.036.342.116a.78.78 0 01.257.324c.063.138.094.273.097.433l.001.012zM7.388 3.997a1.05 1.05 0 00-.277.125.623.623 0 00.002-.15v-.008a.651.651 0 00-.048-.192.37.37 0 00-.096-.141.158.158 0 00-.119-.045c-.042.004-.077.024-.11.065a.372.372 0 00-.07.156.626.626 0 00-.013.205v.008a.634.634 0 00.048.193.367.367 0 00.116.156l-.102.08-.078.056a.706.706 0 01-.16-.24c-.053-.12-.082-.24-.09-.381v-.001a1.071 1.071 0 01.045-.39.668.668 0 01.167-.292.359.359 0 01.264-.118c.084 0 .158.028.235.09a.68.68 0 01.199.271c.053.12.08.24.089.382v.001c.003.06.003.115-.002.17z"/>
      <path fill="#202020" d="M7.806 4.335c.01.034.065.029.097.045.027.014.05.045.08.046.03.001.076-.01.08-.04.005-.038-.052-.063-.088-.077-.047-.019-.107-.028-.151-.003-.01.005-.021.018-.018.03zM7.484 4.335c-.01.034-.065.029-.096.045-.028.014-.05.045-.081.046-.03.001-.076-.01-.08-.04-.005-.038.052-.063.088-.077.047-.019.108-.028.152-.003.01.005.02.018.017.03z"/>
    </svg>
  )
}

function WindowsIcon({ size = 48 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M1.5 3.076l5.481-.757v5.29H1.5V3.076z" fill="#00ADEF"/>
      <path d="M7.648 2.242L14.5 1.2v6.409H7.648V2.242z" fill="#00ADEF"/>
      <path d="M7.648 8.09H14.5v6.41l-6.852-1.043V8.09z" fill="#00ADEF"/>
      <path d="M1.5 8.09h5.481v5.291L1.5 12.624V8.09z" fill="#00ADEF"/>
    </svg>
  )
}

function AppleIcon({ size = 48 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M11.182 1c-.21.932-.862 1.863-1.513 2.425-.652.562-1.513.932-2.375.863.07-.932.722-1.863 1.373-2.356C9.318 1.37 10.32.932 11.182 1zM13.627 5.65c-.14.07-2.095 1.213-2.095 3.638 0 2.793 2.445 3.776 2.515 3.776-.07.07-.42 1.283-1.303 2.565-.792 1.143-1.583 2.286-2.865 2.286-1.234 0-1.653-.724-3.027-.724-1.443 0-1.863.724-3.027.724-1.282 0-2.095-1.213-2.935-2.356C-.07 13.843.001 10.355 1.935 8.489c.932-.932 2.165-1.494 3.237-1.494 1.164 0 2.025.793 3.027.793.932 0 1.653-.793 3.027-.793.652 0 1.793.282 2.4.655z" fill="#555"/>
    </svg>
  )
}

function PlatformIcon({ kernel, size = 48 }) {
  const k = (kernel || '').toLowerCase()
  if (k.includes('windows') || k.includes('mingw') || k.includes('msys')) return <WindowsIcon size={size} />
  if (k.includes('darwin') || k.includes('macos') || k.includes('apple')) return <AppleIcon size={size} />
  return <TuxIcon size={size} />
}

function TargetCard({ target: t, onClick }) {
  const hidden = (t.counts?.files || 0) + (t.counts?.pids || 0) + (t.counts?.content || 0) + (t.counts?.ports || 0)
  const name = t.label || (t.hostname && t.hostname !== '(none)' ? t.hostname : t.id)
  const hiddenParts = [
    t.counts?.files && `${t.counts.files} file${t.counts.files > 1 ? 's' : ''}`,
    t.counts?.pids && `${t.counts.pids} pid${t.counts.pids > 1 ? 's' : ''}`,
    t.counts?.content && `${t.counts.content} content`,
    t.counts?.ports && `${t.counts.ports} port${t.counts.ports > 1 ? 's' : ''}`,
  ].filter(Boolean)

  return (
    <button className="nb-card text-left w-full group" onClick={onClick}>
      <div className="flex items-stretch">
        {/* Left: platform icon column */}
        <div className="flex flex-col items-center justify-center px-5 py-4 border-r-2 border-border bg-bg-2 shrink-0 w-24">
          <PlatformIcon kernel={t.kernel} size={48} />
          <div className="flex items-center gap-1.5 mt-2">
            <span className={`w-2 h-2 rounded-full ${t.online ? 'bg-green pulse' : 'bg-red'}`} />
            <span className={`text-[10px] font-bold uppercase tracking-wider ${t.online ? 'text-green' : 'text-red'}`}>
              {t.online ? 'Online' : 'Offline'}
            </span>
          </div>
        </div>

        {/* Middle: details */}
        <div className="flex-1 min-w-0 px-5 py-4 flex flex-col justify-center gap-1">
          <div className="flex items-baseline gap-3">
            <div className="font-bold text-lg leading-tight truncate">{name}</div>
            <span className="text-[11px] font-mono text-fg-3 shrink-0">{t.id}</span>
          </div>
          <div className="text-xs font-mono text-fg-2">
            {t.kernel || '--'} {t.arch || ''}
            <span className="text-fg-3 mx-2">|</span>
            <span className="text-fg-3">{t.peer || '--'}</span>
          </div>
        </div>

        {/* Right: status bar */}
        <div className="flex items-stretch border-l-2 border-border shrink-0">
          <StatusCell label="Module" value={t.module_hidden ? 'Hidden' : 'Visible'} active={t.module_hidden} />
          <StatusCell label="Persist" value={t.persisted ? 'Yes' : 'No'} active={t.persisted} />
          <StatusCell label="Hidden" value={hidden || '0'} active={hidden > 0} detail={hidden > 0 ? hiddenParts.join(', ') : null} />
          <div className="flex items-center px-3 border-l-2 border-border">
            <ChevronRight size={16} className="text-fg-3 opacity-0 group-hover:opacity-100 transition-opacity" />
          </div>
        </div>
      </div>
    </button>
  )
}

function StatusCell({ label, value, active, detail }) {
  return (
    <div className="flex flex-col items-center justify-center px-4 py-3 border-l-2 border-border first:border-l-0 min-w-[72px]" title={detail || undefined}>
      <span className="text-[9px] font-bold uppercase tracking-widest text-fg-3">{label}</span>
      <span className={`text-sm font-bold font-mono ${active ? 'text-green' : 'text-fg-3'}`}>{value}</span>
    </div>
  )
}
