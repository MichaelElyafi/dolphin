package org.dolphinemu.dolphinemu.features.settings.ui.viewholder;

import android.content.res.Resources;
import android.view.View;
import android.widget.TextView;

import org.dolphinemu.dolphinemu.R;
import org.dolphinemu.dolphinemu.features.settings.model.view.SettingsItem;
import org.dolphinemu.dolphinemu.features.settings.model.view.SingleChoiceSetting;
import org.dolphinemu.dolphinemu.features.settings.model.view.StringSingleChoiceSetting;
import org.dolphinemu.dolphinemu.features.settings.ui.SettingsAdapter;

public final class SingleChoiceViewHolder extends SettingViewHolder
{
  private SettingsItem mItem;

  private TextView mTextSettingName;
  private TextView mTextSettingDescription;

  public SingleChoiceViewHolder(View itemView, SettingsAdapter adapter)
  {
    super(itemView, adapter);
  }

  @Override
  protected void findViews(View root)
  {
    mTextSettingName = (TextView) root.findViewById(R.id.text_setting_name);
    mTextSettingDescription = (TextView) root.findViewById(R.id.text_setting_description);
  }

  @Override
  public void bind(SettingsItem item)
  {
    mItem = item;

    mTextSettingName.setText(item.getNameId());

    if (item.getDescriptionId() > 0)
    {
      // Special cases for SingleChoiceSetting options with long descriptions
      switch (item.getDescriptionId())
      {
        case R.string.shader_compilation_mode_description:
          switch (((SingleChoiceSetting) item).getSelectedValue())
          {
            case 0:
              mTextSettingDescription.setText(R.string.shader_compilation_sync_description);
              break;

            case 1:
              mTextSettingDescription.setText(R.string.shader_compilation_sync_uber_description);
              break;

            case 2:
              mTextSettingDescription.setText(R.string.shader_compilation_async_uber_description);
              break;

            case 3:
              mTextSettingDescription.setText(R.string.shader_compilation_async_skip_description);
              break;
          }
          break;

        default:
          mTextSettingDescription.setText(item.getDescriptionId());
          break;
      }
    }
    else if (item instanceof SingleChoiceSetting)
    {
      SingleChoiceSetting setting = (SingleChoiceSetting) item;
      int selected = setting.getSelectedValue();
      Resources resMgr = mTextSettingDescription.getContext().getResources();
      String[] choices = resMgr.getStringArray(setting.getChoicesId());
      int[] values = resMgr.getIntArray(setting.getValuesId());
      for (int i = 0; i < values.length; ++i)
      {
        if (values[i] == selected)
        {
          mTextSettingDescription.setText(choices[i]);
        }
      }
    }
    else if (item instanceof StringSingleChoiceSetting)
    {
      StringSingleChoiceSetting setting = (StringSingleChoiceSetting) item;
      String[] choices = setting.getChoicesId();
      int valueIndex = setting.getSelectValueIndex();
      if (valueIndex != -1)
        mTextSettingDescription.setText(choices[valueIndex]);
    }
  }

  @Override
  public void onClick(View clicked)
  {
    int position = getAdapterPosition();
    if (mItem instanceof SingleChoiceSetting)
    {
      getAdapter().onSingleChoiceClick((SingleChoiceSetting) mItem, position);
    }
    else if (mItem instanceof StringSingleChoiceSetting)
    {
      getAdapter().onStringSingleChoiceClick((StringSingleChoiceSetting) mItem, position);
    }
  }
}
