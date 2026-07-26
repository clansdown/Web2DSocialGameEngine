<script lang="ts">
  import { onMount, onDestroy, tick } from 'svelte';
  import { startWeedingSession, submitWeedingTurn } from '../../lib/game_state';
  import type { WeedingSessionState, WeedingTurnRequest, WeedingTurnResponse, WeedingActionItem, BoardChange, GridSquare, RawToolConfig, RawWeedingMapConfig, RawPlantConfig } from './weeding_types';
  import type { EndMiniGameResponse } from '../../lib/api';
  import { getTextsRequest } from '../../lib/api';
  import { language } from '../../lib/stores';

  import {
    initEngine, setBoardSize, afterDraw, getMousePosition,
    gameObjects, clear, setBackground,
    setBackgroundMode, setCameraFollowsPlayer
  } from '../../../SimpleGame/ui/src/lib/simplegame';
  import {
    GameObject, GameObjectClass, createText
  } from '../../../SimpleGame/ui/src/lib/gameclasses';
  import type { Text } from '../../../SimpleGame/ui/src/lib/gameclasses';
  import { ButtonClass } from '../../../SimpleGame/ui/src/lib/button';
  import type { Button } from '../../../SimpleGame/ui/src/lib/button';
  import { Column, LayoutJustify } from '../../../SimpleGame/ui/src/lib/layout';

  interface Props {
    characterId: number;
    levelId: number;
    onComplete: (results: EndMiniGameResponse) => void;
    onError: (msg: string) => void;
  }

  let { characterId, levelId, onComplete, onError }: Props = $props();

  let canvas = $state<HTMLCanvasElement | null>(null);
  let loading = $state(true);
  let error = $state<string | null>(null);
  let turnPending = $state(false);

  let sessionId = $state(0);
  let boardData: GridSquare[][] | null = $state(null);
  let gridSize = $state(0);
  let round = $state(1);
  let actionsRemaining = $state(2);
  let equippedToolId = $state<string | null>(null);
  let selectedCrop = $state<string | null>(null);
  let par = $state(0);
  let score = $state(0);
  let won = $state(false);
  let message = $state('');
  let cannotUseText = $state('');

  let tools: RawToolConfig[] = $state([]);
  let plants: RawPlantConfig[] = $state([]);
  let mapMeta: RawWeedingMapConfig | null = $state(null);

  let lastTurnResponse: WeedingTurnResponse | null = $state(null);

  let pendingActions: WeedingActionItem[] = [];
  let lastActionWasSwitch = false;
  let fadingCells: WeedingCellObj[] = [];

  let weedingCells: WeedingCellObj[] = [];
  let toolButtons: Button[] = [];
  let roundText: Text | null = null;
  let actionsText: Text | null = null;
  let messageText: Text | null = null;
  let forfeitButton: Button | null = null;

  const spriteCache: Map<string, HTMLImageElement> = new Map();
  let engineInited = false;
  let overlayRegistered = false;

  const BW = 1600;
  const BH = 900;
  let plantClasses: Map<string, GameObjectClass> = new Map();
  let emptyCellClass: GameObjectClass;
  let cellSize = 0;
  let gridCols = 0;
  let gridRows = 0;

  function computePlantSize(plantType: string | null): { w: number, h: number } {
    if (!plantType) return { w: cellSize, h: cellSize };
    const plant = plants.find((p) => p.id === plantType);
    if (!plant || !plant.sprite || plant.sprite.overlap_x === undefined) return { w: cellSize, h: cellSize };
    const ox = plant.sprite.overlap_x * (cellSize / plant.sprite.render_width);
    const oy = plant.sprite.overlap_y * (cellSize / plant.sprite.render_height);
    return { w: cellSize + 2 * ox, h: cellSize + 2 * oy };
  }

  class WeedingCellObj extends GameObject {
    gridX: number = 0;
    gridY: number = 0;
    plantType: string | null = null;
    progress: number = 0;
    actionsNeeded: number = 0;
    isSmotherCrop: boolean = false;
    isBlocked: boolean = false;
    isAccessible: boolean = false;
    plantObj: GameObject | null = null;

    constructor(cls: GameObjectClass, cx: number, cy: number,
                gridX: number, gridY: number, cell: GridSquare) {
      super(cls, cx, cy);
      this.gridX = gridX;
      this.gridY = gridY;
      this.width = cellSize;
      this.height = cellSize;
      this.hitboxWidth = cellSize;
      this.hitboxHeight = cellSize;
      this.zIndex = 10;
      this.velocity = 0;
      this.standardMovement = false;
      this.updateFromCell(cell);
    }

    updateFromCell(cell: GridSquare): void {
      this.plantType = cell.plant_type;
      this.progress = cell.progress;
      this.actionsNeeded = cell.actions_needed;
      this.isSmotherCrop = cell.is_smother_crop;
      this.isBlocked = cell.is_blocked;
      this.isAccessible = cell.is_accessible;
    }
  }

  function loadImage(url: string): Promise<HTMLImageElement> {
    return new Promise<HTMLImageElement>((resolve) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => {
        console.log(`Failed to load image: ${url}`);
        resolve(img);
      };
      img.src = url;
    });
  }

  async function preloadImages(): Promise<void> {
    const loads: Promise<void>[] = [];
    for (const p of plants) {
      const url = `/images/weeding/plants/${p.sprite.image_file}`;
      loads.push(loadImage(url).then((img: HTMLImageElement) => { spriteCache.set(p.id, img); }));
    }
    for (const t of tools) {
      const url = `/images/weeding/tools/${t.sprite.image_file}`;
      loads.push(loadImage(url).then((img: HTMLImageElement) => { spriteCache.set(`tool_${t.id}`, img); }));
    }
    await Promise.all(loads);
  }

  function isToolValidForCell(cell: WeedingCellObj): boolean {
    if (!equippedToolId) return false;
    if (cell.isBlocked) return false;
    if (cell.isSmotherCrop) return false;
    if (cell.plantType === null) return findTool(equippedToolId)?.can_plant === true;
    const plantCfg = plants.find((p) => p.id === cell.plantType);
    return !!plantCfg?.tools[equippedToolId];
  }

  function drawWeedingOverlay(ctx: CanvasRenderingContext2D, ox: number, oy: number): void {
    if (equippedToolId) {
      const mouse = getMousePosition();
      const img = spriteCache.get(`tool_${equippedToolId}`);
      if (img && img.complete && img.naturalWidth > 0) {
        const cursorSize = cellSize * 0.8;
        const scale = cursorSize / Math.max(img.naturalWidth, img.naturalHeight);
        const sw = img.naturalWidth * scale;
        const sh = img.naturalHeight * scale;
        const drawX = mouse.x - ox - sw / 2;
        const drawY = mouse.y - oy - sh / 2;
        ctx.drawImage(img, drawX, drawY, sw, sh);

        // Check tool validity on hovered cell
        const hoveredCell = weedingCells.find((c) => {
          const hw = c.hitboxWidth / 2;
          const hh = c.hitboxHeight / 2;
          return mouse.x >= c.x - hw && mouse.x <= c.x + hw && mouse.y >= c.y - hh && mouse.y <= c.y + hh;
        });
        const valid = !hoveredCell || isToolValidForCell(hoveredCell);
        if (!valid) {
          ctx.fillStyle = 'rgba(255,0,0,0.35)';
          ctx.fillRect(drawX, drawY, sw, sh);
          if (messageText) messageText.text = cannotUseText;
        } else if (hoveredCell && messageText && messageText.text === cannotUseText) {
          messageText.text = '';
        }
      }
    }

    const hw = cellSize / 2;
    for (const cell of weedingCells) {
      const px = cell.x - ox;
      const py = cell.y - oy;

      if (cell.isBlocked) {
        ctx.fillStyle = 'rgba(80,80,80,0.5)';
        ctx.fillRect(px - hw, py - hw, cellSize, cellSize);
        continue;
      }

      if (equippedToolId && cell.isHovered && cell.isAccessible && !cell.plantType && !cell.isSmotherCrop) {
        const tool = findTool(equippedToolId);
        if (tool && tool.can_plant) {
          ctx.fillStyle = 'rgba(200,230,150,0.35)';
          ctx.fillRect(px - hw, py - hw, cellSize, cellSize);
        }
      }

      if (cell.plantType && !cell.isSmotherCrop && cell.actionsNeeded > 1 && cell.progress > 0) {
        const pct = Math.min(cell.progress / cell.actionsNeeded, 1);
        const barW = cellSize - 8;
        const barY = hw - 6;
        ctx.fillStyle = 'rgba(255,255,255,0.25)';
        ctx.fillRect(px - barW / 2, py + barY - 4, barW, 4);
        ctx.fillStyle = 'rgba(255,255,255,0.7)';
        ctx.fillRect(px - barW / 2, py + barY - 4, barW * pct, 4);
      }
    }

    // Fade-out handler: animate opacity then destroy
    for (let i = fadingCells.length - 1; i >= 0; i--) {
      const c = fadingCells[i];
      if (!c.plantObj) { fadingCells.splice(i, 1); continue; }
      const start = (c.plantObj as any).fadeOutStart;
      const dur = (c.plantObj as any).fadeOutDuration;
      if (start === undefined) { fadingCells.splice(i, 1); continue; }
      const elapsed = performance.now() - start;
      if (elapsed >= dur) {
        c.plantObj.destroy();
        c.plantObj = null;
        fadingCells.splice(i, 1);
      } else {
        c.plantObj.opacity = 1 - (elapsed / dur);
      }
    }
  }

  function updateButtonHighlights(): void {
    for (const btn of toolButtons) {
      const bv = btn.var as { toolId: string; buttonType: string; cropId?: string } | undefined;
      if (!bv) continue;
      if (bv.buttonType === 'tool') {
        btn.color = bv.toolId === equippedToolId ? '#8a7a5a' : '#5a4a3a';
      } else if (bv.buttonType === 'crop') {
        btn.color = (equippedToolId === 'seed_bag' && bv.cropId === selectedCrop) ? '#6a8a5a' : '#3a5a3a';
      }
    }
  }

  function setupWeedingGame(): void {
    setBoardSize(BW, BH);
    setCameraFollowsPlayer(false);

    if (mapMeta && mapMeta.image_file) {
      setBackground([`/images/weeding/maps/${mapMeta.image_file}`]);
      setBackgroundMode('stretch');
    }

    plantClasses.clear();
    for (const plant of plants) {
      const url = `/images/weeding/plants/${plant.sprite.image_file}`;
      const plantHp = plant.hp || 100;
      const gc = new GameObjectClass(plant.id, url, null, plantHp);
      if (plant.damage_sprites) {
        const dotIdx = plant.sprite.image_file.lastIndexOf('.');
        const base = dotIdx >= 0 ? plant.sprite.image_file.slice(0, dotIdx) : plant.sprite.image_file;
        const ext = dotIdx >= 0 ? plant.sprite.image_file.slice(dotIdx) : '';
        for (const threshold of plant.damage_sprites) {
          gc.addDamageSprite(threshold, `/images/weeding/plants/${base}-${threshold}${ext}`);
        }
      }
      plantClasses.set(plant.id, gc);
    }
    emptyCellClass = new GameObjectClass('weed_empty', null, null, 1);

    createGrid();
    createToolBar();
    createHUD();
    updateButtonHighlights();

    if (!overlayRegistered) {
      afterDraw(drawWeedingOverlay);
      overlayRegistered = true;
    }
  }

  function createGrid(): void {
    if (!mapMeta || !boardData) return;
    gridCols = boardData[0].length;
    gridRows = boardData.length;
    const gb = mapMeta.grid_bounds;
    const bx = gb.x * BW;
    const by = gb.y * BH;
    const bw = gb.width * BW;
    const bh = gb.height * BH;
    cellSize = Math.min(bw / gridCols, bh / gridRows);
    const totalW = cellSize * gridCols;
    const totalH = cellSize * gridRows;
    const offsetX = (bw - totalW) / 2;
    const offsetY = (bh - totalH) / 2;

    const cellClass = new GameObjectClass('weed_cell', null, null);
    weedingCells.length = 0;

    for (let y = 0; y < gridRows; y++) {
      for (let x = 0; x < gridCols; x++) {
        const cx = bx + offsetX + x * cellSize + cellSize / 2;
        const cy = by + offsetY + y * cellSize + cellSize / 2;
        const cell = new WeedingCellObj(cellClass, cx, cy, x, y, boardData[y][x]);
        cell.onClick(0, () => {
          console.log(`[Weeding] cell.onClick fired`, { gx: cell.gridX, gy: cell.gridY, turnPending, won, accessible: cell.isAccessible, plantType: cell.plantType, isSmother: cell.isSmotherCrop, tool: equippedToolId, actionsRemaining });
          if (turnPending || won) return;
          handleCellClick(cell);
        });
        gameObjects.add(cell);
        weedingCells.push(cell);

        const plantType = boardData[y][x].plant_type;
        if (plantType) {
          const cls = plantClasses.get(plantType) || emptyCellClass;
          const psize = computePlantSize(plantType);
          const plantObj = new GameObject(cls, cx, cy);
          plantObj.width = psize.w;
          plantObj.height = psize.h;
          plantObj.hitboxWidth = cellSize;
          plantObj.hitboxHeight = cellSize;
          plantObj.zIndex = 20 + y;
          plantObj.direction_x = 0;
          plantObj.direction_y = -1;
          plantObj.velocity = 0;
          plantObj.standardMovement = false;
          plantObj.onClick(0, () => {
            if (turnPending || won) return;
            handleCellClick(cell);
          });
          gameObjects.add(plantObj);
          cell.plantObj = plantObj;
        }
      }
    }
  }

  function createToolBar(): void {
    const gb = mapMeta ? mapMeta.grid_bounds : { x: 0.18, y: 0.18, width: 0.59, height: 0.73 };
    const gridRight = (gb.x + gb.width) * BW;
    const sidebarCenter = gridRight + (BW - gridRight) / 2;
    const btnW = 90;
    const btnH = 80;
    const topAnchor = 70;
    const colHeight = BH - 60 - topAnchor;

    const btnClass = new ButtonClass('tool_btn', null);
    toolButtons.length = 0;

    function iconSize(imgKey: string): { iw: number, ih: number } {
      const img = spriteCache.get(imgKey);
      if (!img || !img.complete || img.naturalWidth === 0) return { iw: 50, ih: 50 };
      const maxIcon = 50;
      const scale = maxIcon / Math.max(img.naturalWidth, img.naturalHeight);
      return { iw: Math.round(img.naturalWidth * scale), ih: Math.round(img.naturalHeight * scale) };
    }

    const col = new Column(sidebarCenter, topAnchor + colHeight / 2);
    col.setSize(btnW, colHeight);
    col.setPadding(0);
    col.setGutter(6);

    // Tool buttons (exclude seed_bag)
    for (const tool of tools) {
      if (tool.id === 'seed_bag') continue;
      const isz = iconSize(`tool_${tool.id}`);
      const btn = btnClass.spawn(
        0, 0,
        null,
        tool.sprite.image_file ? `/images/weeding/tools/${tool.sprite.image_file}` : null,
        {
          width: btnW,
          height: btnH,
          color: '#5a4a3a',
          iconWidth: isz.iw,
          iconHeight: isz.ih,
          iconPadding: 0,
          iconLayout: 'above',
          backgroundOpacity: 0.85,
        }
      );
      btn.zIndex = 100;
      btn.var = { toolId: tool.id, buttonType: 'tool' };
      btn.setOnClick(() => {
        if (turnPending || won) return;
        if (tool.id === equippedToolId) return;
        selectedCrop = null;
        pendingActions.push({ action_type: 'switch_tool', tool_id: tool.id });
        equippedToolId = tool.id;
        if (!lastActionWasSwitch) {
          if (actionsRemaining < 1) return;
          actionsRemaining -= 1;
        }
        lastActionWasSwitch = true;
        updateButtonHighlights();
        updateHUD();
        if (actionsRemaining <= 0) submitBatch();
      });
      col.addChild(btn);
      toolButtons.push(btn);
    }

    // Smother crop buttons (after tools)
    for (const plant of plants) {
      if (!plant.is_smother_crop) continue;
      const isz = iconSize(plant.id);
      const btn = btnClass.spawn(
        0, 0,
        null,
        plant.sprite.image_file ? `/images/weeding/plants/${plant.sprite.image_file}` : null,
        {
          width: btnW,
          height: btnH,
          color: '#3a5a3a',
          iconWidth: isz.iw,
          iconHeight: isz.ih,
          iconPadding: 0,
          iconLayout: 'above',
          backgroundOpacity: 0.85,
        }
      );
      btn.zIndex = 100;
      btn.var = { toolId: 'seed_bag', cropId: plant.id, buttonType: 'crop' };
      btn.setOnClick(() => {
        if (turnPending || won) return;
        if ('seed_bag' === equippedToolId && selectedCrop === plant.id) return;
        selectedCrop = plant.id;
        pendingActions.push({ action_type: 'switch_tool', tool_id: 'seed_bag' });
        equippedToolId = 'seed_bag';
        if (!lastActionWasSwitch) {
          if (actionsRemaining < 1) return;
          actionsRemaining -= 1;
        }
        lastActionWasSwitch = true;
        updateButtonHighlights();
        updateHUD();
        if (actionsRemaining <= 0) submitBatch();
        updateHUD();
      });
      col.addChild(btn);
      toolButtons.push(btn);
    }

    col.setJustify(LayoutJustify.START);
    col.layout();
  }

  function createHUD(): void {
    const hudX = 15;

    roundText = createText('Round: 1  Par: 0', { x: hudX, y: 28 });
    roundText.zIndex = 50;
    roundText.size = 22;
    roundText.textAlign = 'left';
    roundText.foreground = '#ffffff';

    actionsText = createText('Actions: 2/2', { x: hudX, y: 54 });
    actionsText.zIndex = 50;
    actionsText.size = 22;
    actionsText.textAlign = 'left';
    actionsText.foreground = '#ffffff';

    messageText = createText('', { x: BW / 2, y: BH - 24 });
    messageText.zIndex = 50;
    messageText.size = 20;
    messageText.foreground = '#add8e6';
    messageText.textAlign = 'center';

    const gb = mapMeta ? mapMeta.grid_bounds : { x: 0.18, y: 0.18, width: 0.59, height: 0.73 };
    const gridRight = (gb.x + gb.width) * BW;
    const sidebarCenter = gridRight + (BW - gridRight) / 2;
    const btnW = 90;

    const fBtnClass = new ButtonClass('forfeit_btn', null);
    forfeitButton = fBtnClass.spawn(sidebarCenter, BH - 40, 'Forfeit', null, {
      width: btnW,
      height: 36,
      color: '#8b3a3a',
      backgroundOpacity: 0.85,
    });
    forfeitButton.zIndex = 100;
    forfeitButton.setOnClick(() => {
      if (turnPending || won) return;
      handleForfeit();
    });
  }

  function updateHUD(): void {
    if (roundText) roundText.text = `Round: ${round}  Par: ${par}`;
    if (actionsText) actionsText.text = `Actions: ${actionsRemaining}/2`;
    if (messageText) messageText.text = message || '';
  }

  function findTool(toolId: string | null): RawToolConfig | undefined {
    if (!toolId) return undefined;
    return tools.find((t) => t.id === toolId);
  }

  function doArcSwing(obj: GameObject, cell: { x: number; y: number }, cfg: { distance: number; arc_angle: number; duration_ms: number; repetitions: number }, remainingReps: number, onComplete?: () => void): void {
    if (remainingReps <= 0) { obj.destroy(); console.log('[Weeding] doArcSwing complete'); onComplete?.(); return; }
    const timePerRep = cfg.duration_ms / cfg.repetitions / 1000;
    const arcLen = cfg.distance * (cfg.arc_angle * Math.PI / 180);
    const isLast = remainingReps === 1;
    console.log('[Weeding] doArcSwing', { remainingReps, velocity: arcLen / timePerRep, timePerRep, isLast });

    obj.circleAround({
      center: { x: cell.x + cellSize / 2, y: cell.y + cellSize / 2 },
      radius: cfg.distance,
      velocity: arcLen / timePerRep,
      startAngleDeg: -cfg.arc_angle / 2,
      arcDeg: cfg.arc_angle,
      direction: -1,
      facing: { x: -1, y: 0 },
      fadeInTime: 0,
      fadeOutTime: isLast ? 0.1 : 0,
      onComplete: () => doArcSwing(obj, cell, cfg, remainingReps - 1, onComplete),
    });
  }

  function doStraightThrust(obj: GameObject, cell: { x: number; y: number }, cfg: { distance: number; duration_ms: number; repetitions: number }, remainingReps: number, onComplete?: () => void): void {
    if (remainingReps <= 0) { obj.destroy(); console.log('[Weeding] doStraightThrust complete'); onComplete?.(); return; }
    const halfTime = cfg.duration_ms / cfg.repetitions / 2000;
    const isLast = remainingReps === 1;
    console.log('[Weeding] doStraightThrust', { remainingReps, halfTime, isLast });

    obj.x = cell.x;
    obj.y = cell.y - cfg.distance;

    obj.moveTo({ x: cell.x, y: cell.y }, halfTime);
    obj.onArrival(() => {
      if (isLast) {
        obj.fadeOutMillis = 60;
        obj.maxDurationMillis = obj.timeExistedMillis + 60;
        console.log('[Weeding] doStraightThrust final arrival');
        onComplete?.();
        return;
      }
      obj.moveTo({ x: cell.x, y: cell.y - cfg.distance }, halfTime);
      obj.onArrival(() => doStraightThrust(obj, cell, cfg, remainingReps - 1, onComplete));
    });
  }

  function spawnToolAnim(cell: { x: number; y: number }, onComplete?: () => void): void {
    const toolCfg = findTool(equippedToolId);
    const img = spriteCache.get(`tool_${equippedToolId}`);
    console.log('[Weeding] spawnToolAnim', { toolId: equippedToolId, hasAnim: !!toolCfg?.animation, hasImg: !!img, cellX: cell.x, cellY: cell.y });
    if (!toolCfg?.animation) return;

    const cls = new GameObjectClass(`tool_anim`, null, null, 1);
    if (img) {
      cls.image = img;
      cls.defaultWidth = img.width;
      cls.defaultHeight = img.height;
      cls.loaded = true;
    }
    const obj = new GameObject(cls, cell.x, cell.y);
    obj.spriteForwardVector = (toolCfg.sprite as any)?.animation_forward_vector || (toolCfg.sprite as any)?.forward_vector || [0, -1];
    if (img) {
      const toolSize = cellSize * 0.6;
      const aspect = img.naturalWidth / img.naturalHeight;
      if (aspect > 1) {
        obj.width = toolSize;
        obj.height = toolSize / aspect;
      } else {
        obj.width = toolSize * aspect;
        obj.height = toolSize;
      }
    } else {
      obj.width = cellSize * 0.6;
      obj.height = cellSize * 0.6;
    }
    obj.zIndex = 30;
    obj.fadeInMillis = 60;
    gameObjects.add(obj);

    const cellPos = { x: cell.x, y: cell.y };
    const animCfg = toolCfg.animation as any;

    if (animCfg.type === 'arc') {
      console.log('[Weeding] starting arc swing', { reps: animCfg.repetitions, duration: animCfg.duration_ms, arcAngle: animCfg.arc_angle, distance: animCfg.distance });
      doArcSwing(obj, cellPos, animCfg, animCfg.repetitions, onComplete);
    } else {
      console.log('[Weeding] starting straight thrust', { reps: animCfg.repetitions, duration: animCfg.duration_ms, distance: animCfg.distance });
      doStraightThrust(obj, cellPos, animCfg, animCfg.repetitions, onComplete);
    }
  }

  async function handleCellClick(cell: WeedingCellObj): Promise<void> {
    console.log(`[Weeding] handleCellClick`, { gx: cell.gridX, gy: cell.gridY, turnPending, won, accessible: cell.isAccessible, blocked: cell.isBlocked, plantType: cell.plantType, isSmother: cell.isSmotherCrop, tool: equippedToolId, actionsRemaining });
    if (turnPending || won) return;
    if (!cell.isAccessible) return;
    if (cell.isBlocked) return;
    const cost = 1;
    if (cost > actionsRemaining) return;

    if (cell.plantType === null && !cell.isSmotherCrop) {
      if (!equippedToolId) return;
      const toolObj = findTool(equippedToolId);
      console.log('[Weeding] plant attempt', { equippedToolId, canPlant: toolObj?.can_plant, plantCropId: toolObj?.plant_crop_id, selectedCrop, cellGX: cell.gridX, cellGY: cell.gridY, accessible: cell.isAccessible, blocked: cell.isBlocked, actionsRemaining, cost });
      if (toolObj && toolObj.can_plant) {
        actionsRemaining -= cost;
        lastActionWasSwitch = false;
        updateButtonHighlights();
        updateHUD();
        spawnToolAnim({ x: cell.x, y: cell.y }, () => {
          console.log('[Weeding] plant onComplete', { cellGX: cell.gridX, cellGY: cell.gridY, selectedCrop, plantCropId: toolObj.plant_crop_id });
          const cropType = selectedCrop || toolObj.plant_crop_id || 'rye';
          cell.plantType = cropType;
          cell.isSmotherCrop = true;
          cell.progress = 0;
          cell.actionsNeeded = 0;
          applyPlantVisual(cell, cropType);
          pendingActions.push({ action_type: 'plant', tool_id: equippedToolId!, target_x: cell.gridX, target_y: cell.gridY, crop_id: cropType });
          recomputeAccessibility();
          updateHUD();
          checkLocalWin();
          if (actionsRemaining <= 0) submitBatch();
        });
      }
      return;
    }
    if (cell.isSmotherCrop) return;
    if (!equippedToolId) return;
    const plantCfg = plants.find((p) => p.id === cell.plantType);
    const toolEff = plantCfg?.tools[equippedToolId];
    if (!toolEff) return;

    actionsRemaining -= cost;
    lastActionWasSwitch = false;
    updateButtonHighlights();
    updateHUD();
    spawnToolAnim({ x: cell.x, y: cell.y }, () => {
      console.log('[Weeding] use_tool onComplete', { cellGX: cell.gridX, cellGY: cell.gridY, plantType: cell.plantType, toolId: equippedToolId, progress: cell.progress, damage: toolEff.damage });
      cell.progress += toolEff.damage;
      if (cell.plantObj) {
        cell.plantObj.hitpoints = Math.max(0, (plantCfg?.hp ?? 100) - cell.progress);
      }
      if (cell.progress >= (plantCfg?.hp ?? 100)) {
        const plantType = cell.plantType;
        const clearedCells: WeedingCellObj[] = [cell];

        if (toolEff.affects_adjacent && toolEff.adjacent_mode === 'row_or_column') {
          let rowLen = 1, rowStart = cell.gridX, rowEnd = cell.gridX;
          for (let cx = cell.gridX - 1; cx >= 0; --cx) {
            const c = weedingCells.find((wc) => wc.gridX === cx && wc.gridY === cell.gridY);
            if (!c || c.isBlocked || c.isSmotherCrop || c.plantType !== plantType) break;
            rowLen++; rowStart = cx;
          }
          for (let cx = cell.gridX + 1; cx < gridCols; ++cx) {
            const c = weedingCells.find((wc) => wc.gridX === cx && wc.gridY === cell.gridY);
            if (!c || c.isBlocked || c.isSmotherCrop || c.plantType !== plantType) break;
            rowLen++; rowEnd = cx;
          }
          let colLen = 1, colStart = cell.gridY, colEnd = cell.gridY;
          for (let cy = cell.gridY - 1; cy >= 0; --cy) {
            const c = weedingCells.find((wc) => wc.gridX === cell.gridX && wc.gridY === cy);
            if (!c || c.isBlocked || c.isSmotherCrop || c.plantType !== plantType) break;
            colLen++; colStart = cy;
          }
          for (let cy = cell.gridY + 1; cy < gridRows; ++cy) {
            const c = weedingCells.find((wc) => wc.gridX === cell.gridX && wc.gridY === cy);
            if (!c || c.isBlocked || c.isSmotherCrop || c.plantType !== plantType) break;
            colLen++; colEnd = cy;
          }
          if (rowLen >= colLen) {
            for (let cx = rowStart; cx <= rowEnd; ++cx) {
              const c = weedingCells.find((wc) => wc.gridX === cx && wc.gridY === cell.gridY);
              if (c && c !== cell) clearedCells.push(c);
            }
          } else {
            for (let cy = colStart; cy <= colEnd; ++cy) {
              const c = weedingCells.find((wc) => wc.gridX === cell.gridX && wc.gridY === cy);
              if (c && c !== cell) clearedCells.push(c);
            }
          }
        }

        for (const c of clearedCells) {
          c.plantType = null;
          c.isSmotherCrop = false;
          c.progress = 0;
          c.actionsNeeded = 0;
          if (c.plantObj) { c.plantObj.destroy(); c.plantObj = null; }
        }
      }
      pendingActions.push({ action_type: 'use_tool', tool_id: equippedToolId!, target_x: cell.gridX, target_y: cell.gridY });
      recomputeAccessibility();
      updateHUD();
      checkLocalWin();
      if (actionsRemaining <= 0) submitBatch();
    });
  }

  function recomputeAccessibility(): void {
    const visited = new Set<string>();
    const q: { x: number; y: number }[] = [];

    for (let x = 0; x < gridCols; ++x) {
      const y = gridRows - 1;
      const cell = weedingCells.find((c) => c.gridX === x && c.gridY === y);
      if (!cell || cell.isBlocked) continue;
      const key = `${x},${y}`;
      q.push({ x, y });
      visited.add(key);
    }

    const dirs = [[0, -1], [0, 1], [-1, 0], [1, 0]];
    while (q.length > 0) {
      const { x: cx, y: cy } = q.shift()!;
      for (const [dx, dy] of dirs) {
        const nx = cx + dx;
        const ny = cy + dy;
        if (nx < 0 || nx >= gridCols || ny < 0 || ny >= gridRows) continue;
        const key = `${nx},${ny}`;
        if (visited.has(key)) continue;
        const cell = weedingCells.find((c) => c.gridX === nx && c.gridY === ny);
        if (!cell || cell.isBlocked) continue;
        if (cell.plantType === null || cell.isSmotherCrop) {
          visited.add(key);
          q.push({ x: nx, y: ny });
        }
      }
    }

    for (const cell of weedingCells) {
      cell.isAccessible = false;
      if (cell.isBlocked) continue;
      if (cell.isSmotherCrop) { cell.isAccessible = true; continue; }
      const ck = `${cell.gridX},${cell.gridY}`;
      if (visited.has(ck)) { cell.isAccessible = true; continue; }
      if (cell.plantType !== null) {
        for (const [dx, dy] of dirs) {
          const nk = `${cell.gridX + dx},${cell.gridY + dy}`;
          if (visited.has(nk)) { cell.isAccessible = true; break; }
        }
      }
    }
  }

  function applyPlantVisual(cell: WeedingCellObj, plantType: string): void {
    const cls = plantClasses.get(plantType) || emptyCellClass;
    const psize = computePlantSize(plantType);
    const plantCfg = plants.find((p) => p.id === plantType);
    const maxHp = plantCfg?.hp || 100;
    if (cell.plantObj) {
      cell.plantObj.gameclass = cls;
      cell.plantObj.width = psize.w;
      cell.plantObj.height = psize.h;
      cell.plantObj.hitpoints = Math.max(0, maxHp - cell.progress);
    } else {
      const p = new GameObject(cls, cell.x, cell.y);
      p.width = psize.w;
      p.height = psize.h;
      p.hitboxWidth = cellSize;
      p.hitboxHeight = cellSize;
      p.zIndex = 20 + cell.gridY;
      p.direction_x = 0;
      p.direction_y = -1;
      p.velocity = 0;
      p.standardMovement = false;
      p.fadeInMillis = 400;
      p.onClick(0, () => { if (turnPending || won) return; handleCellClick(cell); });
      p.hitpoints = Math.max(0, maxHp - cell.progress);
      gameObjects.add(p);
      cell.plantObj = p;
    }
  }

  function startFadeOut(cell: WeedingCellObj, duration = 200): void {
    if (!cell.plantObj) return;
    (cell.plantObj as any).fadeOutStart = performance.now();
    (cell.plantObj as any).fadeOutDuration = duration;
    fadingCells.push(cell);
  }

  function reconcileBoard(serverBoard: GridSquare[][]): void {
    for (let y = 0; y < gridRows; ++y) {
      for (let x = 0; x < gridCols; ++x) {
        const cell = weedingCells.find((c) => c.gridX === x && c.gridY === y);
        if (!cell) continue;
        const sb = serverBoard[y][x];
        const oldPlantType = cell.plantType;

        // Plant removed — fade out visual
        if (sb.plant_type === null && cell.plantObj !== null) {
          startFadeOut(cell);
        }
        // New plant appeared — fade in visual
        if (sb.plant_type !== null && cell.plantObj === null) {
          applyPlantVisual(cell, sb.plant_type);
        }
        // Plant type changed — swap
        if (sb.plant_type !== null && cell.plantObj !== null && sb.plant_type !== oldPlantType) {
          cell.plantObj.destroy();
          cell.plantObj = null;
          applyPlantVisual(cell, sb.plant_type);
        }

        // Always overwrite cell data from server
        cell.plantType = sb.plant_type;
        cell.progress = sb.progress;
        cell.actionsNeeded = sb.actions_needed;
        cell.isSmotherCrop = sb.is_smother_crop;
        cell.isAccessible = sb.is_accessible;
        if (sb.is_blocked !== undefined) cell.isBlocked = sb.is_blocked;
        // Sync SimpleGame hitpoints with server progress
        if (cell.plantObj) {
          const plantCfg = plants.find((p) => p.id === cell.plantType);
          const maxHp = plantCfg?.hp || 100;
          cell.plantObj.hitpoints = Math.max(0, maxHp - cell.progress);
        }
      }
    }
  }

  function autoFillSmotherCrops(): void {
    const cropId = selectedCrop || findTool('seed_bag')?.plant_crop_id || 'rye';
    for (const cell of weedingCells) {
      if (cell.isBlocked) continue;
      if (cell.plantType === null && !cell.plantObj) {
        applyPlantVisual(cell, cropId);
      }
    }
    recomputeAccessibility();
  }

  function transformFinishedButton(): void {
    if (!forfeitButton) return;
    forfeitButton.text = 'Finished';
    forfeitButton.color = '#5a8a3a';
    forfeitButton.setOnClick(() => {
      if (turnPending) return;
      handleGameWon();
    });
  }

  function checkLocalWin(): void {
    if (won) return;
    for (const cell of weedingCells) {
      if (cell.isBlocked) continue;
      if (cell.plantType !== null && !cell.isSmotherCrop) return;
    }
    submitBatch();
  }

  async function submitBatch(): Promise<void> {
    if (turnPending || pendingActions.length === 0) return;
    console.log('[Weeding] submitBatch', { pendingActions: JSON.parse(JSON.stringify(pendingActions)), actionsRemaining, round });
    turnPending = true;
    if (forfeitButton) forfeitButton.setDisabled(true);
    for (const btn of toolButtons) btn.setDisabled(true);
    try {
      const resp = await submitWeedingTurn({ actions: pendingActions, character_id: characterId, session_id: sessionId } as WeedingTurnRequest);
      console.log('[Weeding] submitBatch response', { round: resp.round, actionsRemaining: resp.actions_remaining, won: resp.won, hasBoard: !!resp.board, message: resp.message });
      handleBatchResponse(resp);
    } catch (e) {
      onError(e instanceof Error ? e.message : 'Turn failed');
      try {
        const state = await startWeedingSession(characterId, levelId);
        round = state.round; actionsRemaining = state.actions_remaining;
        equippedToolId = state.equipped_tool; won = state.won; score = state.score;
        if (state.board) reconcileBoard(state.board);
        updateHUD(); updateButtonHighlights();
      } catch { /* refresh failed — user can forfeit */ }
    } finally {
      pendingActions = []; lastActionWasSwitch = false; turnPending = false;
      if (forfeitButton) forfeitButton.setDisabled(false);
      for (const btn of toolButtons) btn.setDisabled(false);
    }
  }

  async function handleForfeit(): Promise<void> {
    if (turnPending || won) return;
    turnPending = true;
    if (forfeitButton) forfeitButton.setDisabled(true);
    for (const btn of toolButtons) btn.setDisabled(true);
    try {
      const resp = await submitWeedingTurn({ action_type: 'forfeit', character_id: characterId, session_id: sessionId });
      pendingActions = []; lastActionWasSwitch = false;
      round = resp.round; actionsRemaining = resp.actions_remaining; equippedToolId = resp.equipped_tool;
      if (resp.message) message = resp.message;
      updateHUD(); updateButtonHighlights();
      onComplete({ completed: false, score: resp.score ?? 0, new_best_score: resp.new_best_score ?? 0, times_played: resp.times_played ?? 0, all_levels_done: false, base_unlocked: false, rewards: {}, game_phase: resp.game_phase ?? 'initial_mission', next_level_id: null });
    } catch (e) {
      onError(e instanceof Error ? e.message : 'Forfeit failed');
    } finally {
      turnPending = false;
      if (forfeitButton) forfeitButton.setDisabled(false);
      for (const btn of toolButtons) btn.setDisabled(false);
    }
  }

  function handleBatchResponse(resp: WeedingTurnResponse): void {
    console.log('[Weeding] handleBatchResponse', { round: resp.round, actionsRemaining: resp.actions_remaining, equippedTool: resp.equipped_tool, won: resp.won, score: resp.score, message: resp.message, boardSize: resp.board?.length, boardChanges: resp.board_changes?.length });
    lastTurnResponse = resp;
    round = resp.round; actionsRemaining = resp.actions_remaining; equippedToolId = resp.equipped_tool;
    if (resp.equipped_tool !== 'seed_bag') selectedCrop = null;
    if (resp.board) reconcileBoard(resp.board);
    if (resp.won) {
      won = true;
      score = resp.score;
      autoFillSmotherCrops();
      transformFinishedButton();
    }
    if (resp.message) message = resp.message;
    updateHUD(); updateButtonHighlights();
  }

  function handleGameWon(): void {
    if (!lastTurnResponse) return;
    const resp = lastTurnResponse;
    const results: EndMiniGameResponse = {
      completed: resp.completed ?? true,
      score: resp.score ?? score,
      new_best_score: resp.new_best_score ?? score,
      times_played: resp.times_played ?? 1,
      all_levels_done: resp.all_levels_done ?? false,
      base_unlocked: false,
      rewards: resp.rewards ?? {},
      game_phase: resp.game_phase ?? 'sandbox',
      next_level_id: resp.next_level_id ?? null,
    };
    if (resp.completion_bonus) {
      results.completion_bonus = resp.completion_bonus;
    }
    if (resp.land_patent_earned) {
      results.land_patent_earned = true;
    }
    if (resp.duke_right_earned) {
      results.duke_right_earned = true;
    }
    onComplete(results);
  }

  async function startSession(): Promise<void> {
    loading = true;
    error = null;
    try {
      const state = await startWeedingSession(characterId, levelId);
      sessionId = state.session_id;
      boardData = state.board;
      gridSize = state.grid_size;
      round = state.round;
      actionsRemaining = state.actions_remaining;
      equippedToolId = state.equipped_tool;
      par = state.par;
      console.log('[Weeding] startSession response:', { par: state.par, score: state.score, round: state.round, boardSize: state.grid_size });
      updateHUD();
      score = state.score;
      won = state.won;
      message = state.message;

      if (state.available_tools) {
        tools = Array.isArray(state.available_tools)
          ? state.available_tools
          : Object.values(state.available_tools);
      }
      if (state.available_plants) {
        plants = Array.isArray(state.available_plants)
          ? state.available_plants
          : Object.values(state.available_plants);
      }
      if (state.map_metadata) {
        mapMeta = state.map_metadata;
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to start weeding session';
      error = msg;
      onError(msg);
      loading = false;
      return;
    }
    selectedCrop = null;
    try {
      const texts = await getTextsRequest($language, ['wd_error_cannot_use']);
      cannotUseText = texts['wd_error_cannot_use'] || 'This tool cannot be used here.';
    } catch {
      cannotUseText = 'This tool cannot be used here.';
    }
    updateButtonHighlights();
    loading = false;
  }

  function resizeCanvas(): void {
    if (!canvas) return;
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const aspect = BW / BH;
    let w = vw;
    let h = vw / aspect;
    if (h > vh) {
      h = vh;
      w = h * aspect;
    }
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
  }

  onMount(() => {
    const doInit = async () => {
      await startSession();
      await tick();
      if (error || !canvas) return;

      canvas.width = BW;
      canvas.height = BH;
      resizeCanvas();
      window.addEventListener('resize', resizeCanvas);

      await preloadImages();

      const debugDiv = document.createElement('div');
      initEngine(canvas, debugDiv, false, setupWeedingGame);
      engineInited = true;
    };
    doInit();
  });

  onDestroy(() => {
    window.removeEventListener('resize', resizeCanvas);
    if (engineInited) {
      try {
        clear();
      } catch {
        // ignore cleanup errors
      }
      engineInited = false;
      overlayRegistered = false;
    }
  });
</script>

<div class="d-flex flex-column" style="height: 100vh;">
  {#if loading}
    <div class="d-flex justify-content-center align-items-center flex-grow-1">
      <div class="text-center">
        <div class="spinner-border mb-3" role="status"></div>
        <p class="text-muted">Preparing field...</p>
      </div>
    </div>
  {:else if error}
    <div class="d-flex justify-content-center align-items-center flex-grow-1">
      <div class="alert alert-danger">{error}</div>
    </div>
  {:else}
    <div class="flex-grow-1 position-relative" style="min-height: 0; overflow: hidden; background: #000;">
      <canvas
        bind:this={canvas}
        style="display: block; margin: 0 auto; cursor: pointer;"
        aria-label="Weeding game board"
      ></canvas>
    </div>
  {/if}
</div>
