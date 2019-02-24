package org.dolphinemu.dolphinemu.activities;

import android.view.ContextThemeWrapper;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.os.Handler;
import android.preference.PreferenceManager;
import android.support.annotation.IntDef;
import android.support.v4.app.Fragment;
import android.support.v4.app.FragmentActivity;
import android.support.v4.app.FragmentManager;
import android.support.v7.app.AppCompatActivity;
import android.text.TextUtils;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import org.dolphinemu.dolphinemu.NativeLibrary;
import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.features.settings.model.BooleanSetting;
import org.dolphinemu.dolphinemu.features.settings.model.Settings;
import org.dolphinemu.dolphinemu.features.settings.utils.SettingsFile;
import org.dolphinemu.dolphinemu.fragments.EmulationFragment;
import org.dolphinemu.dolphinemu.fragments.MenuFragment;
import org.dolphinemu.dolphinemu.fragments.SaveLoadStateFragment;
import org.dolphinemu.dolphinemu.model.GameFile;
import org.dolphinemu.dolphinemu.services.GameFileCacheService;
import org.dolphinemu.dolphinemu.overlay.InputOverlay;
import org.dolphinemu.dolphinemu.overlay.InputOverlayPointer;
import org.dolphinemu.dolphinemu.ui.main.MainActivity;
import org.dolphinemu.dolphinemu.ui.main.MainPresenter;
import org.dolphinemu.dolphinemu.ui.platform.Platform;
import org.dolphinemu.dolphinemu.utils.ControllerMappingHelper;
import org.dolphinemu.dolphinemu.utils.FileBrowserHelper;
import org.dolphinemu.dolphinemu.utils.Java_GCAdapter;
import org.dolphinemu.dolphinemu.utils.Java_WiimoteAdapter;
import org.dolphinemu.dolphinemu.utils.Rumble;
import org.dolphinemu.dolphinemu.utils.TvUtil;

import java.lang.annotation.Retention;
import java.util.List;

import static java.lang.annotation.RetentionPolicy.SOURCE;

public final class EmulationActivity extends AppCompatActivity
{
  private static final String BACKSTACK_NAME_MENU = "menu";
  private static final String BACKSTACK_NAME_SUBMENU = "submenu";
  public static final int REQUEST_CHANGE_DISC = 1;

  private View mDecorView;
  private EmulationFragment mEmulationFragment;

  private SharedPreferences mPreferences;
  private ControllerMappingHelper mControllerMappingHelper;

  private Settings mSettings;

  private boolean mDeviceHasTouchScreen;
  private boolean mMenuVisible;

  private static boolean sIsGameCubeGame;

  private boolean activityRecreated;
  private String mSelectedTitle;
  private String mSelectedGameId;
  private int mPlatform;
  private String[] mPaths;
  private boolean backPressedOnce = false;

  public static final String EXTRA_SELECTED_GAMES = "SelectedGames";
  public static final String EXTRA_SELECTED_TITLE = "SelectedTitle";
  public static final String EXTRA_SELECTED_GAMEID = "SelectedGameId";
  public static final String EXTRA_PLATFORM = "Platform";

  @Retention(SOURCE)
  @IntDef({MENU_ACTION_EDIT_CONTROLS_PLACEMENT, MENU_ACTION_TOGGLE_CONTROLS, MENU_ACTION_ADJUST_SCALE,
          MENU_ACTION_CHOOSE_CONTROLLER, MENU_ACTION_REFRESH_WIIMOTES, MENU_ACTION_TAKE_SCREENSHOT,
          MENU_ACTION_QUICK_SAVE, MENU_ACTION_QUICK_LOAD, MENU_ACTION_SAVE_ROOT,
          MENU_ACTION_LOAD_ROOT, MENU_ACTION_SAVE_SLOT1, MENU_ACTION_SAVE_SLOT2,
          MENU_ACTION_SAVE_SLOT3, MENU_ACTION_SAVE_SLOT4, MENU_ACTION_SAVE_SLOT5,
          MENU_ACTION_SAVE_SLOT6, MENU_ACTION_LOAD_SLOT1, MENU_ACTION_LOAD_SLOT2,
          MENU_ACTION_LOAD_SLOT3, MENU_ACTION_LOAD_SLOT4, MENU_ACTION_LOAD_SLOT5,
          MENU_ACTION_LOAD_SLOT6, MENU_ACTION_EXIT, MENU_ACTION_CHANGE_DISC,
          MENU_ACTION_RESET_OVERLAY, MENU_SET_IR_SENSITIVITY, MENU_ACTION_CHOOSE_DOUBLETAP})
  public @interface MenuAction
  {
  }

  public static final int MENU_ACTION_EDIT_CONTROLS_PLACEMENT = 0;
  public static final int MENU_ACTION_TOGGLE_CONTROLS = 1;
  public static final int MENU_ACTION_ADJUST_SCALE = 2;
  public static final int MENU_ACTION_CHOOSE_CONTROLLER = 3;
  public static final int MENU_ACTION_REFRESH_WIIMOTES = 4;
  public static final int MENU_ACTION_TAKE_SCREENSHOT = 5;
  public static final int MENU_ACTION_QUICK_SAVE = 6;
  public static final int MENU_ACTION_QUICK_LOAD = 7;
  public static final int MENU_ACTION_SAVE_ROOT = 8;
  public static final int MENU_ACTION_LOAD_ROOT = 9;
  public static final int MENU_ACTION_SAVE_SLOT1 = 10;
  public static final int MENU_ACTION_SAVE_SLOT2 = 11;
  public static final int MENU_ACTION_SAVE_SLOT3 = 12;
  public static final int MENU_ACTION_SAVE_SLOT4 = 13;
  public static final int MENU_ACTION_SAVE_SLOT5 = 14;
  public static final int MENU_ACTION_SAVE_SLOT6 = 15;
  public static final int MENU_ACTION_LOAD_SLOT1 = 16;
  public static final int MENU_ACTION_LOAD_SLOT2 = 17;
  public static final int MENU_ACTION_LOAD_SLOT3 = 18;
  public static final int MENU_ACTION_LOAD_SLOT4 = 19;
  public static final int MENU_ACTION_LOAD_SLOT5 = 20;
  public static final int MENU_ACTION_LOAD_SLOT6 = 21;
  public static final int MENU_ACTION_EXIT = 22;
  public static final int MENU_ACTION_CHANGE_DISC = 23;
  public static final int MENU_ACTION_JOYSTICK_REL_CENTER = 24;
  public static final int MENU_ACTION_RUMBLE = 25;
  public static final int MENU_ACTION_RESET_OVERLAY = 26;
  public static final int MENU_SET_IR_SENSITIVITY = 27;
  public static final int MENU_ACTION_CHOOSE_DOUBLETAP = 28;


  private static SparseIntArray buttonsActionsMap = new SparseIntArray();

  static
  {
    buttonsActionsMap.append(R.id.menu_emulation_edit_layout,
            EmulationActivity.MENU_ACTION_EDIT_CONTROLS_PLACEMENT);
    buttonsActionsMap.append(R.id.menu_emulation_toggle_controls,
            EmulationActivity.MENU_ACTION_TOGGLE_CONTROLS);
    buttonsActionsMap
            .append(R.id.menu_emulation_adjust_scale, EmulationActivity.MENU_ACTION_ADJUST_SCALE);
    buttonsActionsMap.append(R.id.menu_emulation_choose_controller,
            EmulationActivity.MENU_ACTION_CHOOSE_CONTROLLER);
    buttonsActionsMap
            .append(R.id.menu_refresh_wiimotes, EmulationActivity.MENU_ACTION_REFRESH_WIIMOTES);
    buttonsActionsMap
            .append(R.id.menu_emulation_screenshot, EmulationActivity.MENU_ACTION_TAKE_SCREENSHOT);

    buttonsActionsMap.append(R.id.menu_quicksave, EmulationActivity.MENU_ACTION_QUICK_SAVE);
    buttonsActionsMap.append(R.id.menu_quickload, EmulationActivity.MENU_ACTION_QUICK_LOAD);
    buttonsActionsMap
            .append(R.id.menu_emulation_save_root, EmulationActivity.MENU_ACTION_SAVE_ROOT);
    buttonsActionsMap
            .append(R.id.menu_emulation_load_root, EmulationActivity.MENU_ACTION_LOAD_ROOT);
    buttonsActionsMap.append(R.id.menu_emulation_save_1, EmulationActivity.MENU_ACTION_SAVE_SLOT1);
    buttonsActionsMap.append(R.id.menu_emulation_save_2, EmulationActivity.MENU_ACTION_SAVE_SLOT2);
    buttonsActionsMap.append(R.id.menu_emulation_save_3, EmulationActivity.MENU_ACTION_SAVE_SLOT3);
    buttonsActionsMap.append(R.id.menu_emulation_save_4, EmulationActivity.MENU_ACTION_SAVE_SLOT4);
    buttonsActionsMap.append(R.id.menu_emulation_save_5, EmulationActivity.MENU_ACTION_SAVE_SLOT5);
    buttonsActionsMap.append(R.id.menu_emulation_load_1, EmulationActivity.MENU_ACTION_LOAD_SLOT1);
    buttonsActionsMap.append(R.id.menu_emulation_load_2, EmulationActivity.MENU_ACTION_LOAD_SLOT2);
    buttonsActionsMap.append(R.id.menu_emulation_load_3, EmulationActivity.MENU_ACTION_LOAD_SLOT3);
    buttonsActionsMap.append(R.id.menu_emulation_load_4, EmulationActivity.MENU_ACTION_LOAD_SLOT4);
    buttonsActionsMap.append(R.id.menu_emulation_load_5, EmulationActivity.MENU_ACTION_LOAD_SLOT5);
    buttonsActionsMap.append(R.id.menu_change_disc, EmulationActivity.MENU_ACTION_CHANGE_DISC);
    buttonsActionsMap.append(R.id.menu_exit, EmulationActivity.MENU_ACTION_EXIT);
    buttonsActionsMap.append(R.id.menu_emulation_joystick_rel_center,
            EmulationActivity.MENU_ACTION_JOYSTICK_REL_CENTER);
    buttonsActionsMap.append(R.id.menu_emulation_rumble, EmulationActivity.MENU_ACTION_RUMBLE);
    buttonsActionsMap
            .append(R.id.menu_emulation_reset_overlay, EmulationActivity.MENU_ACTION_RESET_OVERLAY);
    buttonsActionsMap.append(R.id.menu_emulation_set_ir_sensitivity,
            EmulationActivity.MENU_SET_IR_SENSITIVITY);
    buttonsActionsMap.append(R.id.menu_emulation_choose_doubletap,
            EmulationActivity.MENU_ACTION_CHOOSE_DOUBLETAP);
  }

  private static String[] scanForSecondDisc(GameFile gameFile)
  {
    GameFile secondFile = GameFileCacheService.findSecondDisc(gameFile);
    if (secondFile == null)
      return new String[]{gameFile.getPath()};
    else
      return new String[]{gameFile.getPath(), secondFile.getPath()};
  }

  public static void launch(FragmentActivity activity, GameFile gameFile)
  {
    Intent launcher = new Intent(activity, EmulationActivity.class);

    launcher.putExtra(EXTRA_SELECTED_GAMES, scanForSecondDisc(gameFile));
    launcher.putExtra(EXTRA_SELECTED_TITLE, gameFile.getTitle());
    launcher.putExtra(EXTRA_SELECTED_GAMEID, gameFile.getGameId());
    launcher.putExtra(EXTRA_PLATFORM, gameFile.getPlatform());
    activity.startActivityForResult(launcher, MainPresenter.REQUEST_EMULATE_GAME);
  }

  @Override
  protected void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);

    // Find the EmulationFragment
    mEmulationFragment = (EmulationFragment) getSupportFragmentManager()
            .findFragmentById(R.id.frame_emulation_fragment);

    if (savedInstanceState == null)
    {
      // Get params we were passed
      Intent gameToEmulate = getIntent();
      mPaths = gameToEmulate.getStringArrayExtra(EXTRA_SELECTED_GAMES);
      mSelectedTitle = gameToEmulate.getStringExtra(EXTRA_SELECTED_TITLE);
      mSelectedGameId = gameToEmulate.getStringExtra(EXTRA_SELECTED_GAMEID);
      mPlatform = gameToEmulate.getIntExtra(EXTRA_PLATFORM, 0);
      activityRecreated = false;
    }
    else
    {
      // Could have recreated the activity(rotate) before creating the fragment. If the fragment
      // doesn't exist, treat this as a new start.
      activityRecreated = mEmulationFragment != null;
      restoreState(savedInstanceState);
    }

    mSettings = new Settings();
    mSettings.loadSettings(null);

    // TODO: The accurate way to find out which console we're emulating is to
    // first launch emulation and then ask the core which console we're emulating
    sIsGameCubeGame = Platform.fromNativeInt(mPlatform) == Platform.GAMECUBE;
    mDeviceHasTouchScreen = getPackageManager().hasSystemFeature("android.hardware.touchscreen");
    mControllerMappingHelper = new ControllerMappingHelper();

    int themeId;
    if (mDeviceHasTouchScreen)
    {
      themeId = R.style.DolphinEmulationBase;

      // Get a handle to the Window containing the UI.
      mDecorView = getWindow().getDecorView();
      mDecorView.setOnSystemUiVisibilityChangeListener(visibility ->
      {
        if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0)
        {
          // Go back to immersive fullscreen mode in 3s
          Handler handler = new Handler(getMainLooper());
          handler.postDelayed(this::enableFullscreenImmersive, 3000 /* 3s */);
        }
      });
      // Set these options now so that the SurfaceView the game renders into is the right size.
      enableFullscreenImmersive();
      Toast.makeText(this, getString(R.string.emulation_touch_button_help), Toast.LENGTH_LONG)
              .show();
    }
    else
    {
      themeId = R.style.DolphinEmulationTvBase;
    }

    setTheme(themeId);

    Java_GCAdapter.manager = (UsbManager) getSystemService(Context.USB_SERVICE);
    Java_WiimoteAdapter.manager = (UsbManager) getSystemService(Context.USB_SERVICE);
    Rumble.initRumble(this);

    setContentView(R.layout.activity_emulation);


    BooleanSetting lockLandscapeSetting =
            (BooleanSetting) mSettings.getSection(Settings.SECTION_INI_CORE)
                    .getSetting(SettingsFile.KEY_LOCK_LANDSCAPE);
    boolean lockLandscape = lockLandscapeSetting == null || lockLandscapeSetting.getValue();
    // Force landscape if set
    if (mDeviceHasTouchScreen && lockLandscape)
    {
      setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    if (!(mDeviceHasTouchScreen && lockLandscape &&
            getResources().getConfiguration().orientation == Configuration.ORIENTATION_PORTRAIT) &&
            mEmulationFragment == null)
    {
      mEmulationFragment = EmulationFragment.newInstance(mPaths);
      getSupportFragmentManager().beginTransaction()
              .add(R.id.frame_emulation_fragment, mEmulationFragment)
              .commit();
    }

    if (mDeviceHasTouchScreen)
    {
      setTitle(mSelectedTitle);
    }

    mPreferences = PreferenceManager.getDefaultSharedPreferences(this);

  }

  @Override
  protected void onSaveInstanceState(Bundle outState)
  {
    if (!isChangingConfigurations())
    {
      mEmulationFragment.saveTemporaryState();
    }
    outState.putStringArray(EXTRA_SELECTED_GAMES, mPaths);
    outState.putString(EXTRA_SELECTED_TITLE, mSelectedTitle);
    outState.putString(EXTRA_SELECTED_GAMEID, mSelectedGameId);
    outState.putInt(EXTRA_PLATFORM, mPlatform);
    super.onSaveInstanceState(outState);
  }

  protected void restoreState(Bundle savedInstanceState)
  {
    mPaths = savedInstanceState.getStringArray(EXTRA_SELECTED_GAMES);
    mSelectedTitle = savedInstanceState.getString(EXTRA_SELECTED_TITLE);
    mSelectedGameId = savedInstanceState.getString(EXTRA_SELECTED_GAMEID);
    mPlatform = savedInstanceState.getInt(EXTRA_PLATFORM);
  }

  @Override
  protected void onStop()
  {
    super.onStop();
  }

  @Override
  public void onBackPressed()
  {
    if (!mDeviceHasTouchScreen)
    {
      boolean popResult = getSupportFragmentManager().popBackStackImmediate(
              BACKSTACK_NAME_SUBMENU, FragmentManager.POP_BACK_STACK_INCLUSIVE);
      if (!popResult)
      {
        toggleMenu();
      }
    }
    else
    {
      if (backPressedOnce)
      {
        mEmulationFragment.stopEmulation();
        finish();
      }
      else
      {
        backPressedOnce = true;
        Toast.makeText(this, "Press back again to exit", Toast.LENGTH_LONG).show();
        new Handler().postDelayed(() -> backPressedOnce = false, 3000);
      }
    }
  }

  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent result)
  {
    switch (requestCode)
    {
      case REQUEST_CHANGE_DISC:
        // If the user picked a file, as opposed to just backing out.
        if (resultCode == MainActivity.RESULT_OK)
        {
          String newDiscPath = FileBrowserHelper.getSelectedDirectory(result);
          if (!TextUtils.isEmpty(newDiscPath))
          {
            NativeLibrary.ChangeDisc(newDiscPath);
          }
        }
        break;
    }
  }

  private void enableFullscreenImmersive()
  {
    // It would be nice to use IMMERSIVE_STICKY, but that doesn't show the toolbar.
    mDecorView.setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_IMMERSIVE);
  }

  private void toggleMenu()
  {
    boolean result = getSupportFragmentManager().popBackStackImmediate(
            BACKSTACK_NAME_MENU, FragmentManager.POP_BACK_STACK_INCLUSIVE);
    mMenuVisible = false;

    if (!result)
    {
      // Removing the menu failed, so that means it wasn't visible. Add it.
      Fragment fragment = MenuFragment.newInstance(mSelectedTitle);
      getSupportFragmentManager().beginTransaction()
              .setCustomAnimations(
                      R.animator.menu_slide_in_from_left,
                      R.animator.menu_slide_out_to_left,
                      R.animator.menu_slide_in_from_left,
                      R.animator.menu_slide_out_to_left)
              .add(R.id.frame_menu, fragment)
              .addToBackStack(BACKSTACK_NAME_MENU)
              .commit();
      mMenuVisible = true;
    }
  }

  @Override
  public boolean onCreateOptionsMenu(Menu menu)
  {
    // Inflate the menu; this adds items to the action bar if it is present.
    if (sIsGameCubeGame)
    {
      getMenuInflater().inflate(R.menu.menu_emulation, menu);
    }
    else
    {
      getMenuInflater().inflate(R.menu.menu_emulation_wii, menu);
    }

    BooleanSetting enableSaveStates =
            (BooleanSetting) mSettings.getSection(Settings.SECTION_INI_CORE)
                    .getSetting(SettingsFile.KEY_ENABLE_SAVE_STATES);
    if (enableSaveStates != null && enableSaveStates.getValue())
    {
      menu.findItem(R.id.menu_quicksave).setVisible(true);
      menu.findItem(R.id.menu_quickload).setVisible(true);
      menu.findItem(R.id.menu_emulation_save_root).setVisible(true);
      menu.findItem(R.id.menu_emulation_load_root).setVisible(true);
    }

    // Populate the checkbox value for joystick center on touch
    menu.findItem(R.id.menu_emulation_joystick_rel_center)
            .setChecked(mPreferences.getBoolean("joystickRelCenter", true));
    menu.findItem(R.id.menu_emulation_rumble)
            .setChecked(mPreferences.getBoolean("phoneRumble", true));

    return true;
  }

  @SuppressWarnings("WrongConstant")
  @Override
  public boolean onOptionsItemSelected(MenuItem item)
  {
    int action = buttonsActionsMap.get(item.getItemId(), -1);
    if (action >= 0)
    {
      if (item.isCheckable())
      {
        // Need to pass a reference to the item to set the checkbox state, since it is not done automatically
        handleCheckableMenuAction(action, item);
      }
      else
      {
        handleMenuAction(action);
      }
    }
    return true;
  }

  public void handleCheckableMenuAction(@MenuAction int menuAction, MenuItem item)
  {
    switch (menuAction)
    {
      case MENU_ACTION_JOYSTICK_REL_CENTER:
        item.setChecked(!item.isChecked());
        toggleJoystickRelCenter(item.isChecked());
        break;
      case MENU_ACTION_RUMBLE:
        item.setChecked(!item.isChecked());
        toggleRumble(item.isChecked());
        break;
    }
  }

  public void handleMenuAction(@MenuAction int menuAction)
  {
    switch (menuAction)
    {
      // Edit the placement of the controls
      case MENU_ACTION_EDIT_CONTROLS_PLACEMENT:
        editControlsPlacement();
        break;

      // Reset overlay placement
      case MENU_ACTION_RESET_OVERLAY:
        resetOverlay();
        break;

      // Enable/Disable specific buttons or the entire input overlay.
      case MENU_ACTION_TOGGLE_CONTROLS:
        toggleControls();
        return;

      // Adjust the scale of the overlay controls.
      case MENU_ACTION_ADJUST_SCALE:
        adjustScale();
        return;

      // (Wii games only) Change the controller for the input overlay.
      case MENU_ACTION_CHOOSE_CONTROLLER:
        chooseController();
        return;

      case MENU_ACTION_REFRESH_WIIMOTES:
        NativeLibrary.RefreshWiimotes();
        return;

      // Screenshot capturing
      case MENU_ACTION_TAKE_SCREENSHOT:
        NativeLibrary.SaveScreenShot();
        return;

      // Quick save / load
      case MENU_ACTION_QUICK_SAVE:
        NativeLibrary.SaveState(9, false);
        return;

      case MENU_ACTION_QUICK_LOAD:
        NativeLibrary.LoadState(9);
        return;

      // TV Menu only
      case MENU_ACTION_SAVE_ROOT:
        if (!mDeviceHasTouchScreen)
        {
          showSubMenu(SaveLoadStateFragment.SaveOrLoad.SAVE);
        }
        return;

      case MENU_ACTION_LOAD_ROOT:
        if (!mDeviceHasTouchScreen)
        {
          showSubMenu(SaveLoadStateFragment.SaveOrLoad.LOAD);
        }
        return;

      // Save state slots
      case MENU_ACTION_SAVE_SLOT1:
        NativeLibrary.SaveState(0, false);
        return;

      case MENU_ACTION_SAVE_SLOT2:
        NativeLibrary.SaveState(1, false);
        return;

      case MENU_ACTION_SAVE_SLOT3:
        NativeLibrary.SaveState(2, false);
        return;

      case MENU_ACTION_SAVE_SLOT4:
        NativeLibrary.SaveState(3, false);
        return;

      case MENU_ACTION_SAVE_SLOT5:
        NativeLibrary.SaveState(4, false);
        return;

      case MENU_ACTION_SAVE_SLOT6:
        NativeLibrary.SaveState(5, false);
        return;

      // Load state slots
      case MENU_ACTION_LOAD_SLOT1:
        NativeLibrary.LoadState(0);
        return;

      case MENU_ACTION_LOAD_SLOT2:
        NativeLibrary.LoadState(1);
        return;

      case MENU_ACTION_LOAD_SLOT3:
        NativeLibrary.LoadState(2);
        return;

      case MENU_ACTION_LOAD_SLOT4:
        NativeLibrary.LoadState(3);
        return;

      case MENU_ACTION_LOAD_SLOT5:
        NativeLibrary.LoadState(4);
        return;

      case MENU_ACTION_LOAD_SLOT6:
        NativeLibrary.LoadState(5);
        return;

      case MENU_ACTION_CHANGE_DISC:
        FileBrowserHelper.openFilePicker(this, REQUEST_CHANGE_DISC);
        return;

      case MENU_SET_IR_SENSITIVITY:
        setIRSensitivity();
        return;

      case MENU_ACTION_CHOOSE_DOUBLETAP:
        chooseDoubleTapButton();
        return;

      case MENU_ACTION_EXIT:
        // ATV menu is built using a fragment, this will pop that fragment before emulation ends.
        if (TvUtil.isLeanback(getApplicationContext()))
          toggleMenu();  // Hide the menu (it will be showing since we just clicked it)
        mEmulationFragment.stopEmulation();
        finish();
        return;
    }
  }

  private void toggleJoystickRelCenter(boolean state)
  {
    final SharedPreferences.Editor editor = mPreferences.edit();
    editor.putBoolean("joystickRelCenter", state);
    editor.commit();
  }

  private void toggleRumble(boolean state)
  {
    final SharedPreferences.Editor editor = mPreferences.edit();
    editor.putBoolean("phoneRumble", state);
    editor.apply();
    Rumble.setPhoneVibrator(state, this);
  }


  private void editControlsPlacement()
  {
    if (mEmulationFragment.isConfiguringControls())
    {
      mEmulationFragment.stopConfiguringControls();
    }
    else
    {
      mEmulationFragment.startConfiguringControls();
    }
  }

  // Gets button presses
  @Override
  public boolean dispatchKeyEvent(KeyEvent event)
  {
    if (mMenuVisible)
    {
      return super.dispatchKeyEvent(event);
    }

    int action;

    switch (event.getAction())
    {
      case KeyEvent.ACTION_DOWN:
        // Handling the case where the back button is pressed.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK)
        {
          onBackPressed();
          return true;
        }

        // Normal key events.
        action = NativeLibrary.ButtonState.PRESSED;
        break;
      case KeyEvent.ACTION_UP:
        action = NativeLibrary.ButtonState.RELEASED;
        break;
      default:
        return false;
    }
    InputDevice input = event.getDevice();
    return NativeLibrary.onGamePadEvent(input.getDescriptor(), event.getKeyCode(), action);
  }

  private void toggleControls()
  {
    final SharedPreferences.Editor editor = mPreferences.edit();
    boolean[] enabledButtons = new boolean[14];
    AlertDialog.Builder builder = new AlertDialog.Builder(new ContextThemeWrapper(this, R.style.GameSettingsDialogWindow));
    builder.setTitle(R.string.emulation_toggle_controls);
    if (sIsGameCubeGame || mPreferences.getInt("wiiController", 3) == 0)
    {
      for (int i = 0; i < enabledButtons.length; i++)
      {
        enabledButtons[i] = mPreferences.getBoolean("buttonToggleGc" + i, true);
      }
      builder.setMultiChoiceItems(R.array.gcpadButtons, enabledButtons,
              (dialog, indexSelected, isChecked) -> editor
                      .putBoolean("buttonToggleGc" + indexSelected, isChecked));
    }
    else if (mPreferences.getInt("wiiController", 3) == 4)
    {
      for (int i = 0; i < enabledButtons.length; i++)
      {
        enabledButtons[i] = mPreferences.getBoolean("buttonToggleClassic" + i, true);
      }
      builder.setMultiChoiceItems(R.array.classicButtons, enabledButtons,
              (dialog, indexSelected, isChecked) -> editor
                      .putBoolean("buttonToggleClassic" + indexSelected, isChecked));
    }
    else
    {
      for (int i = 0; i < enabledButtons.length; i++)
      {
        enabledButtons[i] = mPreferences.getBoolean("buttonToggleWii" + i, true);
      }
      if (mPreferences.getInt("wiiController", 3) == 3)
      {
        builder.setMultiChoiceItems(R.array.nunchukButtons, enabledButtons,
                (dialog, indexSelected, isChecked) -> editor
                        .putBoolean("buttonToggleWii" + indexSelected, isChecked));
      }
      else
      {
        builder.setMultiChoiceItems(R.array.wiimoteButtons, enabledButtons,
                (dialog, indexSelected, isChecked) -> editor
                        .putBoolean("buttonToggleWii" + indexSelected, isChecked));
      }
    }
    builder.setNeutralButton(getString(R.string.emulation_toggle_all),
            (dialogInterface, i) -> mEmulationFragment.toggleInputOverlayVisibility());
    builder.setPositiveButton(getString(R.string.ok), (dialogInterface, i) ->
    {
      editor.apply();

      mEmulationFragment.refreshInputOverlay();
    });

    AlertDialog alertDialog = builder.create();
    alertDialog.show();
  }

  public void chooseDoubleTapButton()
  {
    final SharedPreferences.Editor editor = mPreferences.edit();
    AlertDialog.Builder builder = new AlertDialog.Builder(new ContextThemeWrapper(this, R.style.GameSettingsDialogWindow));

    int currentController =
            mPreferences.getInt("wiiController", InputOverlay.OVERLAY_WIIMOTE_NUNCHUCK);

    int currentValue = mPreferences.getInt("doubleTapButton",
            InputOverlayPointer.DOUBLE_TAP_OPTIONS.get(InputOverlayPointer.DOUBLE_TAP_A));

    int buttonList = currentController == InputOverlay.OVERLAY_WIIMOTE_CLASSIC ?
            R.array.doubleTapWithClassic : R.array.doubleTap;

    if (currentController != InputOverlay.OVERLAY_WIIMOTE_CLASSIC &&
            currentValue == InputOverlay.OVERLAY_WIIMOTE_CLASSIC)
    {
      currentValue = InputOverlay.OVERLAY_WIIMOTE;
    }

    builder.setSingleChoiceItems(buttonList, currentValue, (DialogInterface dialog, int which) ->
            editor.putInt("doubleTapButton", InputOverlayPointer.DOUBLE_TAP_OPTIONS.get(which)));

    builder.setPositiveButton(getString(R.string.ok), (dialogInterface, i) ->
    {
      editor.commit();
      mEmulationFragment.initInputPointer();
    });

    AlertDialog alertDialog = builder.create();
    alertDialog.show();
  }

  private void adjustScale()
  {
    LayoutInflater inflater = LayoutInflater.from(this);
    View view = inflater.inflate(R.layout.dialog_seekbar, null);

    final SeekBar seekbar = (SeekBar) view.findViewById(R.id.seekbar);
    final TextView value = (TextView) view.findViewById(R.id.text_value);
    final TextView units = (TextView) view.findViewById(R.id.text_units);

    seekbar.setMax(150);
    seekbar.setProgress(mPreferences.getInt("controlScale", 50));
    seekbar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener()
    {
      public void onStartTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }

      public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser)
      {
        value.setText(String.valueOf(progress + 50));
      }

      public void onStopTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }
    });

    value.setText(String.valueOf(seekbar.getProgress() + 50));
    units.setText("%");

    AlertDialog.Builder builder = new AlertDialog.Builder(new ContextThemeWrapper(this, R.style.GameSettingsDialogWindow));
    builder.setTitle(R.string.emulation_control_scale);
    builder.setView(view);
    builder.setPositiveButton(getString(R.string.ok), (dialogInterface, i) ->
    {
      SharedPreferences.Editor editor = mPreferences.edit();
      editor.putInt("controlScale", seekbar.getProgress());
      editor.apply();

      mEmulationFragment.refreshInputOverlay();
    });

    AlertDialog alertDialog = builder.create();
    alertDialog.show();
  }

  private void chooseController()
  {
    final SharedPreferences.Editor editor = mPreferences.edit();
    AlertDialog.Builder builder = new AlertDialog.Builder(new ContextThemeWrapper(this, R.style.GameSettingsDialogWindow));
    builder.setTitle(R.string.emulation_choose_controller);
    builder.setSingleChoiceItems(R.array.controllersEntries,
            mPreferences.getInt("wiiController", 3),
            (dialog, indexSelected) ->
            {
              editor.putInt("wiiController", indexSelected);

              NativeLibrary.SetConfig("WiimoteNew.ini", "Wiimote1", "Extension",
                      getResources().getStringArray(R.array.controllersValues)[indexSelected]);
            });
    builder.setPositiveButton(getString(R.string.ok), (dialogInterface, i) ->
    {
      editor.apply();

      mEmulationFragment.refreshInputOverlay();

      Toast.makeText(getApplication(), R.string.emulation_controller_changed, Toast.LENGTH_SHORT)
              .show();
    });

    AlertDialog alertDialog = builder.create();
    alertDialog.show();

  }

  private void setIRSensitivity()
  {
    int irHeight = Integer.valueOf(
            mPreferences.getString(SettingsFile.KEY_WIIBIND_IR_HEIGHT + mSelectedGameId, "50"));

    LayoutInflater inflater = LayoutInflater.from(this);
    View view = inflater.inflate(R.layout.dialog_ir_sensitivity, null);

    TextView mTextSliderValueHeight = (TextView) view.findViewById(R.id.text_ir_height);
    TextView units = (TextView) view.findViewById(R.id.text_ir_height_units);
    SeekBar seekbarHeight = view.findViewById(R.id.seekbar_height);

    mTextSliderValueHeight.setText(String.valueOf(irHeight));
    units.setText(getString(R.string.height));
    seekbarHeight.setMax(100);
    seekbarHeight.setProgress(irHeight);
    seekbarHeight.setKeyProgressIncrement(5);
    seekbarHeight.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener()
    {
      @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser)
      {
        mTextSliderValueHeight.setText(String.valueOf(progress));
      }

      @Override public void onStartTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }

      @Override public void onStopTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }
    });

    int irWidth = Integer.valueOf(
            mPreferences.getString(SettingsFile.KEY_WIIBIND_IR_WIDTH + mSelectedGameId, "50"));

    TextView mTextSliderValueWidth = (TextView) view.findViewById(R.id.text_ir_width);
    TextView unitsWidth = (TextView) view.findViewById(R.id.text_ir_width_units);
    SeekBar seekbarWidth = view.findViewById(R.id.seekbar_width);

    mTextSliderValueWidth.setText(String.valueOf(irWidth));
    unitsWidth.setText(getString(R.string.width));
    seekbarWidth.setMax(100);
    seekbarWidth.setProgress(irWidth);
    seekbarWidth.setKeyProgressIncrement(5);
    seekbarWidth.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener()
    {
      @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser)
      {
        mTextSliderValueWidth.setText(String.valueOf(progress));
      }

      @Override public void onStartTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }

      @Override public void onStopTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }
    });


    int irCenter = Integer.valueOf(
            mPreferences.getString(SettingsFile.KEY_WIIBIND_IR_CENTER + mSelectedGameId, "50"));

    TextView mTextSliderValueCenter = (TextView) view.findViewById(R.id.text_ir_center);
    TextView unitsCenter = (TextView) view.findViewById(R.id.text_ir_center_units);
    SeekBar seekbarCenter = view.findViewById(R.id.seekbar_center);

    mTextSliderValueCenter.setText(String.valueOf(irCenter));
    unitsCenter.setText(getString(R.string.center));
    seekbarCenter.setMax(100);
    seekbarCenter.setProgress(irCenter);
    seekbarCenter.setKeyProgressIncrement(5);
    seekbarCenter.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener()
    {
      @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser)
      {
        mTextSliderValueCenter.setText(String.valueOf(progress));
      }

      @Override public void onStartTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }

      @Override public void onStopTrackingTouch(SeekBar seekBar)
      {
        // Do nothing
      }
    });

    AlertDialog.Builder builder = new AlertDialog.Builder(new ContextThemeWrapper(this, R.style.GameSettingsDialogWindow));
    builder.setTitle(getString(R.string.emulation_ir_sensitivity));
    builder.setView(view);
    builder.setPositiveButton(R.string.ok, (dialogInterface, i) ->
    {
      SettingsFile.saveSingleCustomSetting(mSelectedGameId, Settings.SECTION_CONTROLS,
              SettingsFile.KEY_WIIBIND_IR_HEIGHT, mTextSliderValueHeight.getText().toString());
      SettingsFile.saveSingleCustomSetting(mSelectedGameId, Settings.SECTION_CONTROLS,
              SettingsFile.KEY_WIIBIND_IR_WIDTH, mTextSliderValueWidth.getText().toString());
      SettingsFile.saveSingleCustomSetting(mSelectedGameId, Settings.SECTION_CONTROLS,
              SettingsFile.KEY_WIIBIND_IR_CENTER, mTextSliderValueCenter.getText().toString());

      NativeLibrary.ReloadWiimoteConfig();

      SharedPreferences.Editor editor = mPreferences.edit();
      editor.putString(SettingsFile.KEY_WIIBIND_IR_HEIGHT + mSelectedGameId,
              mTextSliderValueHeight.getText().toString());
      editor.putString(SettingsFile.KEY_WIIBIND_IR_WIDTH + mSelectedGameId,
              mTextSliderValueWidth.getText().toString());
      editor.putString(SettingsFile.KEY_WIIBIND_IR_CENTER + mSelectedGameId,
              mTextSliderValueCenter.getText().toString());
      editor.apply();
    });
    builder.setNegativeButton(R.string.cancel, (dialogInterface, i) ->
    {
      // Do nothing
    });
    builder.show();
  }

  private void resetOverlay()
  {
    new AlertDialog.Builder(new ContextThemeWrapper(this, R.style.GameSettingsDialogWindow))
            .setTitle(getString(R.string.emulation_touch_overlay_reset))
            .setPositiveButton(R.string.yes, (dialogInterface, i) ->
            {
              mEmulationFragment.resetInputOverlay();
            })
            .setNegativeButton(R.string.cancel, (dialogInterface, i) ->
            {
            })
            .create()
            .show();
  }

  @Override
  public boolean dispatchGenericMotionEvent(MotionEvent event)
  {
    if (mMenuVisible)
    {
      return false;
    }

    if (((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == 0))
    {
      return super.dispatchGenericMotionEvent(event);
    }

    // Don't attempt to do anything if we are disconnecting a device.
    if (event.getActionMasked() == MotionEvent.ACTION_CANCEL)
      return true;

    InputDevice input = event.getDevice();
    List<InputDevice.MotionRange> motions = input.getMotionRanges();

    for (InputDevice.MotionRange range : motions)
    {
      int axis = range.getAxis();
      float origValue = event.getAxisValue(axis);
      float value = mControllerMappingHelper.scaleAxis(input, axis, origValue);
      // If the input is still in the "flat" area, that means it's really zero.
      // This is used to compensate for imprecision in joysticks.
      if (Math.abs(value) > range.getFlat())
      {
        NativeLibrary.onGamePadMoveEvent(input.getDescriptor(), axis, value);
      }
      else
      {
        NativeLibrary.onGamePadMoveEvent(input.getDescriptor(), axis, 0.0f);
      }
    }

    return true;
  }

  private void showSubMenu(SaveLoadStateFragment.SaveOrLoad saveOrLoad)
  {
    // Get rid of any visible submenu
    getSupportFragmentManager().popBackStack(
            BACKSTACK_NAME_SUBMENU, FragmentManager.POP_BACK_STACK_INCLUSIVE);

    Fragment fragment = SaveLoadStateFragment.newInstance(saveOrLoad);
    getSupportFragmentManager().beginTransaction()
            .setCustomAnimations(
                    R.animator.menu_slide_in_from_right,
                    R.animator.menu_slide_out_to_right,
                    R.animator.menu_slide_in_from_right,
                    R.animator.menu_slide_out_to_right)
            .replace(R.id.frame_submenu, fragment)
            .addToBackStack(BACKSTACK_NAME_SUBMENU)
            .commit();
  }

  public boolean deviceHasTouchScreen()
  {
    return mDeviceHasTouchScreen;
  }

  public String getSelectedTitle()
  {
    return mSelectedTitle;
  }

  public static boolean isGameCubeGame()
  {
    return sIsGameCubeGame;
  }

  public boolean isActivityRecreated()
  {
    return activityRecreated;
  }

  public Settings getSettings()
  {
    return mSettings;
  }

  public void initInputPointer()
  {
    if (deviceHasTouchScreen())
      mEmulationFragment.initInputPointer();
  }
}
