package io.github.teamgdb.yakumo;

import android.content.Intent;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Looper;
import android.os.Bundle;
import android.os.Build;
import android.graphics.Rect;
import android.graphics.Insets;
import android.view.DisplayCutout;
import android.view.WindowInsets;
import android.view.HapticFeedbackConstants;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.app.AlertDialog;
import android.provider.DocumentsContract;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.libsdl.app.SDLActivity;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.Shadows;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.shadows.ShadowActivity;
import org.robolectric.shadows.ShadowAlertDialog;
import org.robolectric.shadows.ShadowContentResolver;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implements;
import org.robolectric.annotation.Implementation;
import org.robolectric.shadows.ShadowViewRootImpl;
import org.robolectric.annotation.ConscryptMode;
import org.robolectric.util.ReflectionHelpers;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.*;

/** Public Android contracts, without starting SDL's native game library. */
@RunWith(RobolectricTestRunner.class)
@Config(sdk = {29, 35}, manifest = Config.NONE)
@ConscryptMode(ConscryptMode.Mode.OFF)
public class YakumoActivityTest {
    @Before
    public void resetActivity() {
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSingleton", null);
    }

    @After
    public void clearActivity() {
        resetActivity();
    }

    @Test
    public void missingActivityReturnsSafeJniDefaults() {
        assertArrayEquals(new int[4], YakumoActivity.cutoutInsets());
        assertNull(YakumoActivity.pickFolder());
        assertNull(YakumoActivity.pickDocument());
        assertNull(YakumoActivity.create("content://test/tree/root/document/root", "file", false));
        assertEquals(-1, YakumoActivity.openDocument("content://test/document/file", "r"));
        YakumoActivity.hapticTick();
        YakumoActivity.relaunch();
    }

    @Test
    public void landscapeContractIgnoresResizableAndOrientationHints() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        for (boolean resizable : new boolean[]{false, true}) {
            activity.setOrientationBis(100, 200, resizable, "Portrait");
            assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE, activity.getRequestedOrientation());
            activity.setOrientationBis(200, 100, resizable, null);
            assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE, activity.getRequestedOrientation());
        }
    }

    @Test
    public void treeRootPreservesAuthorityAndEscapedDocumentId() {
        assertEquals("content://documents.test/tree/primary%3AGames/document/primary%3AGames",
            YakumoActivity.treeRoot("content://documents.test/tree/primary%3AGames"));
    }

    @Test
    public void directoryListingPreservesTypesUrisAndClosesItsCursor() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSingleton", activity);
        ListingProvider provider = new ListingProvider();
        provider.attachInfo(RuntimeEnvironment.getApplication(), null);
        ShadowContentResolver.registerProviderInternal("documents.test", provider);
        String folder = "content://documents.test/tree/root/document/root";
        assertArrayEquals(new String[]{
            "d/Saves/content://documents.test/tree/root/document/saves",
            "f/Options.ini/content://documents.test/tree/root/document/options"
        }, YakumoActivity.listFolder(folder));
        assertTrue(provider.cursor.isClosed());
        provider.empty = true;
        assertArrayEquals(new String[0], YakumoActivity.listFolder(folder));
        assertTrue(provider.cursor.isClosed());
        provider.fail = true;
        assertNull(YakumoActivity.listFolder(folder));
        assertNull(YakumoActivity.create(folder, "New folder", true));
        assertNull(YakumoActivity.create(folder, "Options.ini", false));
        assertEquals(-1, YakumoActivity.openDocument(folder, "w"));
    }

    public static final class ListingProvider extends ContentProvider {
        boolean empty;
        boolean fail;
        MatrixCursor cursor;
        @Override public boolean onCreate() { return true; }
        @Override public Cursor query(Uri uri, String[] projection, String selection,
                                      String[] selectionArgs, String sortOrder) {
            if (fail) throw new SecurityException("Synthetic denied document access");
            assertTrue(uri.toString().endsWith("/children"));
            assertArrayEquals(new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE}, projection);
            cursor = new MatrixCursor(projection);
            if (!empty) {
                cursor.addRow(new Object[]{"saves", "Saves", DocumentsContract.Document.MIME_TYPE_DIR});
                cursor.addRow(new Object[]{"options", "Options.ini", "application/octet-stream"});
            }
            return cursor;
        }
        @Override public String getType(Uri uri) { return "application/octet-stream"; }
        @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
        @Override public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException(); }
        @Override public int update(Uri uri, ContentValues values, String selection, String[] args) {
            throw new UnsupportedOperationException();
        }
    }

    @Test
    public void messageBoxKeepsButtonsAvailableAndUsesKeyboardDefaults() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        Bundle args = new Bundle();
        args.putString("title", "Public test");
        args.putString("message", new String(new char[2000]).replace('\0', 'x'));
        args.putIntArray("buttonFlags", new int[]{1, 2});
        args.putIntArray("buttonIds", new int[]{7, 9});
        args.putStringArray("buttonTexts", new String[]{"Continue", "Cancel"});
        activity.messageboxCreateAndShow(args);
        AlertDialog dialog = ShadowAlertDialog.getLatestAlertDialog();
        assertTrue(dialog.isShowing());
        Button button = findButton(dialog.getWindow().getDecorView(), "Continue");
        assertNotNull("Long text must retain an actionable button", button);
        assertTrue(button.performClick());
        assertFalse(dialog.isShowing());
        assertEquals(7, ((int[]) ReflectionHelpers.getField(activity, "messageboxSelection"))[0]);
        activity.messageboxCreateAndShow(args);
        dialog = ShadowAlertDialog.getLatestAlertDialog();
        dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK));
        dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK));
        assertFalse(dialog.isShowing());
        assertEquals(9, ((int[]) ReflectionHelpers.getField(activity, "messageboxSelection"))[0]);
    }

    /** SDL's documented library-load failure keeps Android lifecycle tests public. */
    public static class MissingNativeLibraryActivity extends YakumoActivity {
        @Override public void loadLibraries() {
            throw new UnsatisfiedLinkError("Synthetic unavailable SDL native library");
        }
    }

    @Test
    public void startupAppliesCutoutPolicyEvenWhenNativeLibraryIsUnavailable() {
        ReflectionHelpers.setStaticField(SDLActivity.class, "mActivityCreated", false);
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSDLMainFinished", false);
        MissingNativeLibraryActivity activity =
            Robolectric.buildActivity(MissingNativeLibraryActivity.class).create().get();
        assertEquals(Build.VERSION.SDK_INT >= 30
            ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES,
            activity.getWindow().getAttributes().layoutInDisplayCutoutMode);
        assertTrue("SDL must report unavailable native startup", ShadowAlertDialog.getLatestAlertDialog().isShowing());
        ShadowAlertDialog.getLatestAlertDialog().dismiss();
        ReflectionHelpers.setStaticField(SDLActivity.class, "mActivityCreated", false);
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSDLMainFinished", false);
    }

    /** Synthetic OS insets isolate cutout handling from a physical display. */
    @Implements(className = "android.view.ViewRootImpl", isInAndroidSdk = false)
    public static class InsetsViewShadow extends ShadowViewRootImpl {
        static WindowInsets rootInsets;
        @Implementation protected WindowInsets getWindowInsets(boolean forceConstruct) {
            return rootInsets != null ? rootInsets : new WindowInsets.Builder().build();
        }
    }

    @Test
    @Config(shadows = InsetsViewShadow.class)
    public void cutoutUsesPhysicalInsetsAndIgnoresSystemBars() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSingleton", activity);
        try {
            InsetsViewShadow.rootInsets = null;
            assertArrayEquals(new int[4], YakumoActivity.cutoutInsets());
            InsetsViewShadow.rootInsets = new WindowInsets.Builder().build();
            activity.getWindowManager().addView(activity.getWindow().getDecorView(), new WindowManager.LayoutParams());
            Shadows.shadowOf(Looper.getMainLooper()).idle();
            assertArrayEquals(new int[4], YakumoActivity.cutoutInsets());
            DisplayCutout cutout = new DisplayCutout(new Rect(11, 12, 13, 14),
                java.util.Collections.singletonList(new Rect(0, 0, 11, 100)));
            WindowInsets.Builder builder = new WindowInsets.Builder().setDisplayCutout(cutout);
            if (Build.VERSION.SDK_INT >= 30) {
                builder.setInsets(WindowInsets.Type.displayCutout(), Insets.of(11, 12, 13, 14));
                builder.setInsets(WindowInsets.Type.systemBars(), Insets.of(31, 32, 33, 34));
            }
            InsetsViewShadow.rootInsets = builder.build();
            assertSame(InsetsViewShadow.rootInsets, activity.getWindow().getDecorView().getRootWindowInsets());
            assertArrayEquals(new int[]{11, 12, 13, 14}, YakumoActivity.cutoutInsets());
        } finally {
            activity.getWindowManager().removeViewImmediate(activity.getWindow().getDecorView());
            InsetsViewShadow.rootInsets = null;
        }
    }

    @Test
    public void messageBoxStacksNarrowButtonsAndHandlesEnterOnlyOnRelease() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        Bundle args = new Bundle();
        args.putString("title", "Public layout contract");
        args.putString("message", "A bounded message");
        args.putIntArray("buttonFlags", new int[]{1, 2});
        args.putIntArray("buttonIds", new int[]{17, 19});
        args.putStringArray("buttonTexts", new String[]{"Continue with selected settings", "Cancel installation"});
        activity.messageboxCreateAndShow(args);
        AlertDialog dialog = ShadowAlertDialog.getLatestAlertDialog();
        Button button = findButton(dialog.getWindow().getDecorView(), "Continue with selected settings");
        LinearLayout bar = (LinearLayout) button.getParent();
        int height = View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
        bar.measure(View.MeasureSpec.makeMeasureSpec(120, View.MeasureSpec.EXACTLY), height);
        assertEquals(LinearLayout.VERTICAL, bar.getOrientation());
        assertEquals(ViewGroup.LayoutParams.MATCH_PARENT, button.getLayoutParams().width);
        bar.measure(View.MeasureSpec.makeMeasureSpec(4000, View.MeasureSpec.EXACTLY), height);
        assertEquals(LinearLayout.HORIZONTAL, bar.getOrientation());
        assertEquals(ViewGroup.LayoutParams.WRAP_CONTENT, button.getLayoutParams().width);
        dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
        assertTrue("Key down must not dismiss the dialog", dialog.isShowing());
        dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
        assertFalse(dialog.isShowing());
        assertEquals(17, ((int[]) ReflectionHelpers.getField(activity, "messageboxSelection"))[0]);
        args.putIntArray("buttonFlags", new int[]{0, 0});
        activity.messageboxCreateAndShow(args);
        dialog = ShadowAlertDialog.getLatestAlertDialog();
        dialog.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
        assertTrue("No default means Enter cannot choose a button", dialog.isShowing());
        dialog.dismiss();
    }

    @Test
    public void hapticTickPostsVirtualKeyFeedback() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSingleton", activity);
        View view = activity.getWindow().getDecorView();
        activity.getWindowManager().addView(view, new WindowManager.LayoutParams());
        try {
            YakumoActivity.hapticTick();
            Shadows.shadowOf(Looper.getMainLooper()).idle();
            assertEquals(HapticFeedbackConstants.VIRTUAL_KEY, Shadows.shadowOf(view).lastHapticFeedbackPerformed());
        } finally {
            activity.getWindowManager().removeViewImmediate(view);
        }
    }

    @Test
    public void relaunchWithoutLauncherDoesNotStartAnotherActivity() {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSingleton", activity);
        YakumoActivity.relaunch();
        assertNull(Shadows.shadowOf(activity).getNextStartedActivity());
    }

    private Button findButton(View view, String text) {
        if (view instanceof Button && ((Button) view).getText().toString().equals(text)) return (Button) view;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int index = 0; index < group.getChildCount(); ++index) {
                Button found = findButton(group.getChildAt(index), text);
                if (found != null) return found;
            }
        }
        return null;
    }

    @Test
    public void folderPickerReturnsSelectedTreeAndCancellation() throws Exception {
        exercisePicker(true, YakumoActivity.RESULT_OK,
            "content://documents.test/tree/root");
        exercisePicker(true, YakumoActivity.RESULT_CANCELED, null);
    }

    @Test
    public void documentPickerReturnsSelectedFileAndHandlesMissingResult() throws Exception {
        exercisePicker(false, YakumoActivity.RESULT_OK,
            "content://documents.test/document/file");
        exercisePicker(false, YakumoActivity.RESULT_OK, null);
    }

    private void exercisePicker(boolean folder, int status, String selected) throws Exception {
        YakumoActivity activity = Robolectric.buildActivity(YakumoActivity.class).get();
        ReflectionHelpers.setStaticField(SDLActivity.class, "mSingleton", activity);
        FutureTask<String> result = new FutureTask<>(folder ? YakumoActivity::pickFolder : YakumoActivity::pickDocument);
        Thread worker = new Thread(result, "document-picker-test");
        worker.start();
        try {
            ShadowActivity.IntentForResult request = null;
            for (int attempt = 0; attempt < 100 && request == null; ++attempt) {
                Shadows.shadowOf(Looper.getMainLooper()).idle();
                request = Shadows.shadowOf(activity).getNextStartedActivityForResult();
                if (request == null) Thread.sleep(10);
            }
            assertNotNull("The picker must launch on the UI thread", request);
            assertEquals(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT,
                request.intent.getAction());
            assertTrue((request.intent.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0);
            if (folder) {
                assertTrue((request.intent.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0);
            } else {
                assertEquals("*/*", request.intent.getType());
                assertTrue(request.intent.hasCategory(Intent.CATEGORY_OPENABLE));
            }
            assertFalse("The game thread waits until a result arrives", result.isDone());
            Intent selection = selected == null ? null : new Intent().setData(android.net.Uri.parse(selected));
            activity.onActivityResult(request.requestCode, status, selection);
            assertEquals(selected, result.get(2, TimeUnit.SECONDS));
        } finally {
            worker.interrupt();
            worker.join(2000);
            assertFalse("The test must not leave a waiting picker thread", worker.isAlive());
        }
    }
}
