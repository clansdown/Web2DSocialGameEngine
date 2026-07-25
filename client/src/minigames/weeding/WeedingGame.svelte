<script lang="ts">
  import { onMount, onDestroy, tick } from 'svelte';
  import { startWeedingSession, submitWeedingTurn } from '../../lib/game_state';
  import type { WeedingSessionState, WeedingTurnRequest, WeedingTurnResponse, BoardChange, GridSquare, RawToolConfig, RawWeedingMapConfig, RawPlantConfig } from './weeding_types';
  import type { EndMiniGameResponse } from '../../lib/api';

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

  let tools: RawToolConfig[] = $state([]);
  let plants: RawPlantConfig[] = $state([]);
  let mapMeta: RawWeedingMapConfig | null = $state(null);

  let lastTurnResponse: WeedingTurnResponse | null = $state(null);

  let weedingCells: WeedingCellObj[] = [];
  let toolButtons: Button[] = [];
  let roundText: Text | null = null;
  let actionsText: Text | null = null;
  let parScoreText: Text | null = null;
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

  function drawWeedingOverlay(ctx: CanvasRenderingContext2D, ox: number, oy: number): void {
    if (equippedToolId) {
      const mouse = getMousePosition();
      const img = spriteCache.get(`tool_${equippedToolId}`);
      if (img && img.complete && img.naturalWidth > 0) {
        const cursorSize = cellSize * 0.8;
        const scale = cursorSize / Math.max(img.naturalWidth, img.naturalHeight);
        const sw = img.naturalWidth * scale;
        const sh = img.naturalHeight * scale;
        ctx.drawImage(img, mouse.x - ox - sw / 2, mouse.y - oy - sh / 2, sw, sh);
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

      if (cell.plantType && !cell.isSmotherCrop && cell.actionsNeeded > 1) {
        const pct = Math.min(cell.progress / cell.actionsNeeded, 1);
        const barW = cellSize - 8;
        const barY = hw - 6;
        ctx.fillStyle = 'rgba(255,255,255,0.25)';
        ctx.fillRect(px - barW / 2, py + barY - 4, barW, 4);
        ctx.fillStyle = 'rgba(255,255,255,0.7)';
        ctx.fillRect(px - barW / 2, py + barY - 4, barW * pct, 4);
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
      plantClasses.set(plant.id, new GameObjectClass(plant.id, url, null, 1));
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
        gameObjects.add(cell);
        weedingCells.push(cell);
        cell.onClick(0, () => {
          if (turnPending || won) return;
          handleCellClick(cell);
        });

        const plantType = boardData[y][x].plant_type;
        if (plantType) {
          const cls = plantClasses.get(plantType) || emptyCellClass;
          const psize = computePlantSize(plantType);
          const plantObj = new GameObject(cls, cx, cy);
          plantObj.width = psize.w;
          plantObj.height = psize.h;
          plantObj.hitboxWidth = 0;
          plantObj.hitboxHeight = 0;
          plantObj.zIndex = 20 + y;
          plantObj.direction_x = 0;
          plantObj.direction_y = -1;
          plantObj.velocity = 0;
          plantObj.standardMovement = false;
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
        selectedCrop = null;
        doTurn('switch_tool', tool.id, -1, -1);
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
        selectedCrop = plant.id;
        doTurn('switch_tool', 'seed_bag', -1, -1);
      });
      col.addChild(btn);
      toolButtons.push(btn);
    }

    col.setJustify(LayoutJustify.START);
    col.layout();
  }

  function createHUD(): void {
    const hudX = 15;

    roundText = createText('Round: 1', { x: hudX, y: 28 });
    roundText.zIndex = 50;
    roundText.size = 22;
    roundText.textAlign = 'left';
    roundText.foreground = '#ffffff';

    actionsText = createText('Actions: 2/2', { x: hudX, y: 54 });
    actionsText.zIndex = 50;
    actionsText.size = 22;
    actionsText.textAlign = 'left';
    actionsText.foreground = '#ffffff';

    parScoreText = createText('Par: 0  Score: 0', { x: hudX, y: 80 });
    parScoreText.zIndex = 50;
    parScoreText.size = 22;
    parScoreText.textAlign = 'left';
    parScoreText.foreground = '#ffffff';

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
      doTurn('forfeit', '', -1, -1);
    });
  }

  function updateHUD(): void {
    if (roundText) roundText.text = `Round: ${round}`;
    if (actionsText) actionsText.text = `Actions: ${actionsRemaining}/2`;
    if (parScoreText) parScoreText.text = `Par: ${par}  Score: ${score}`;
    if (messageText) messageText.text = message || '';
  }

  function updateCells(changes: BoardChange[]): void {
    for (const change of changes) {
      const cell = weedingCells.find((c) => c.gridX === change.x && c.gridY === change.y);
      if (!cell) continue;

      const oldPlantType = cell.plantType;
      cell.plantType = change.plant_type;
      cell.progress = change.progress;
      cell.actionsNeeded = change.actions_needed;
      cell.isSmotherCrop = change.is_smother_crop;
      cell.isAccessible = change.is_accessible;
      if (change.is_blocked !== undefined) cell.isBlocked = change.is_blocked;

      if (change.plant_type !== oldPlantType) {
        // Destroy old plant object if any
        if (cell.plantObj) {
          cell.plantObj.destroy();
          cell.plantObj = null;
        }
        // Create new plant object if needed
        if (change.plant_type) {
          const cls = plantClasses.get(change.plant_type) || emptyCellClass;
          const psize = computePlantSize(change.plant_type);
          const plantObj = new GameObject(cls, cell.x, cell.y);
          plantObj.width = psize.w;
          plantObj.height = psize.h;
          plantObj.hitboxWidth = 0;
          plantObj.hitboxHeight = 0;
          plantObj.zIndex = 20 + cell.gridY;
          plantObj.direction_x = 0;
          plantObj.direction_y = -1;
          plantObj.velocity = 0;
          plantObj.standardMovement = false;
          gameObjects.add(plantObj);
          cell.plantObj = plantObj;
        }
      }
    }
  }

  function findTool(toolId: string | null): RawToolConfig | undefined {
    if (!toolId) return undefined;
    return tools.find((t) => t.id === toolId);
  }

  async function handleCellClick(cell: WeedingCellObj): Promise<void> {
    if (turnPending || won) return;
    if (!cell.isAccessible) return;
    if (cell.isBlocked) return;

    if (cell.plantType === null && !cell.isSmotherCrop) {
      if (!equippedToolId) return;
      const toolObj = findTool(equippedToolId);
      if (toolObj && toolObj.can_plant) {
        await doTurn('plant', equippedToolId, cell.gridX, cell.gridY);
      }
      return;
    }
    if (cell.isSmotherCrop) return;

    if (!equippedToolId) return;
    await doTurn('use_tool', equippedToolId, cell.gridX, cell.gridY);
  }

  async function doTurn(actionType: string, toolId: string, targetX: number, targetY: number): Promise<void> {
    if (turnPending) return;
    turnPending = true;
    if (forfeitButton) forfeitButton.setDisabled(true);
    for (const btn of toolButtons) btn.setDisabled(true);

    try {
      if (actionType === 'forfeit') {
        const resp = await submitWeedingTurn({
          action_type: 'forfeit',
          character_id: characterId,
          session_id: sessionId,
        });
        handleTurnResponse(resp);
        const results: EndMiniGameResponse = {
          completed: false,
          score: resp.score ?? 0,
          new_best_score: resp.new_best_score ?? 0,
          times_played: resp.times_played ?? 0,
          all_levels_done: false,
          base_unlocked: false,
          rewards: {},
          game_phase: resp.game_phase ?? 'initial_mission',
          next_level_id: null,
        };
        onComplete(results);
        return;
      }

      const turnBody: WeedingTurnRequest = {
        action_type: actionType as 'use_tool' | 'switch_tool' | 'plant' | 'forfeit',
        character_id: characterId,
        session_id: sessionId,
      };
      if (actionType === 'switch_tool' || actionType === 'use_tool' || actionType === 'plant') {
        turnBody.tool_id = toolId;
      }
      if (targetX >= 0) turnBody.target_x = targetX;
      if (targetY >= 0) turnBody.target_y = targetY;

      const resp = await submitWeedingTurn(turnBody);
      handleTurnResponse(resp);
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Turn failed';
      onError(msg);
    } finally {
      turnPending = false;
      if (forfeitButton) forfeitButton.setDisabled(false);
      for (const btn of toolButtons) btn.setDisabled(false);
    }
  }

  function handleTurnResponse(resp: WeedingTurnResponse): void {
    lastTurnResponse = resp;
    round = resp.round;
    actionsRemaining = resp.actions_remaining;
    equippedToolId = resp.equipped_tool;

    if (resp.equipped_tool !== 'seed_bag') {
      selectedCrop = null;
    }

    if (resp.board_changes) {
      updateCells(resp.board_changes);
    }

    if (resp.won) {
      won = true;
      score = resp.score;
    }
    if (resp.message) message = resp.message;
    updateHUD();
    updateButtonHighlights();

    if (resp.won) {
      handleGameWon();
    }
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
